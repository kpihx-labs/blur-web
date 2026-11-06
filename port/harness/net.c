// Tiny HTTP + WebSocket server (no dependencies). Serves ./web and streams render messages to one browser client;
// browser input events come back through net_poll_input(). Designed for localhost use only.
#include "emu.h"
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
// sockets are SOCKET handles (small integers in practice) carried in an int like the POSIX fds below
#define read(fd, b, n) recv((SOCKET)(fd), (char *)(b), (int)(n), 0)
#define write(fd, b, n) send((SOCKET)(fd), (const char *)(b), (int)(n), 0)
#define close(fd) closesocket((SOCKET)(fd))
#define SIGPIPE 13
struct iovec { void *iov_base; size_t iov_len; };
static ssize_t writev(int fd, struct iovec *iov, int cnt) {
    WSABUF wb[4]; DWORD sent = 0; if (cnt > 4) cnt = 4; for (int i = 0; i < cnt; i++) { wb[i].buf = iov[i].iov_base; wb[i].len = (ULONG)iov[i].iov_len; }
    return WSASend((SOCKET)fd, wb, (DWORD)cnt, &sent, 0, NULL, NULL) == 0 ? (ssize_t)sent : -1;
}
static char *strcasestr(const char *h, const char *n) { size_t l = strlen(n); for (; *h; h++) if (!strncasecmp(h, n, l)) return (char *)h; return NULL; }
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <netinet/tcp.h>
#endif

// ---- SHA-1 + base64 (WebSocket handshake) ------------------------------------------------------------------------------
typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; int n; } Sha1;
static uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }
static void sha1_block(Sha1 *s, const uint8_t *p) {
    uint32_t w[80], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | p[4 * i + 1] << 16 | p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; i++) {
        uint32_t f, k; if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; } else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; } else { f = b ^ c ^ d; k = 0xca62c1d6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i]; e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}
static void sha1(const uint8_t *data, size_t len, uint8_t out[20]) {
    Sha1 s = {{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0}, 0, {0}, 0};
    size_t i = 0; for (; i + 64 <= len; i += 64) sha1_block(&s, data + i);
    uint8_t tail[128] = {0}; size_t r = len - i; memcpy(tail, data + i, r); tail[r] = 0x80;
    size_t tl = r + 1 + 8 <= 64 ? 64 : 128; uint64_t bits = (uint64_t)len * 8;
    for (int k = 0; k < 8; k++) tail[tl - 1 - k] = (uint8_t)(bits >> (8 * k));
    for (size_t o = 0; o < tl; o += 64) sha1_block(&s, tail + o);
    for (int k = 0; k < 5; k++) { out[4 * k] = s.h[k] >> 24; out[4 * k + 1] = s.h[k] >> 16; out[4 * k + 2] = s.h[k] >> 8; out[4 * k + 3] = (uint8_t)s.h[k]; }
}
static void b64(const uint8_t *in, int n, char *out) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0; for (int i = 0; i < n; i += 3) {
        uint32_t v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = T[v >> 18]; out[o++] = T[(v >> 12) & 63]; out[o++] = i + 1 < n ? T[(v >> 6) & 63] : '='; out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = 0;
}

// ---- outgoing queue (emulation thread -> network thread) ----------------------------------------------------------------------------
static pthread_mutex_t qm = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t qc = PTHREAD_COND_INITIALIZER, qspace = PTHREAD_COND_INITIALIZER;
static uint8_t *outbuf; static size_t outlen, outcap; static int client_fd = -1; static int server_up;
#define OUT_LIMIT (256u << 20)


int net_client_connected(void) {
    pthread_mutex_lock(&qm);
    int connected = client_fd >= 0;
    pthread_mutex_unlock(&qm);
    return connected;
}
void net_send(const void *data, size_t n) {
    if (!server_up) return;
    pthread_mutex_lock(&qm);
    if (client_fd < 0) { pthread_mutex_unlock(&qm); return; }                       // nobody listening: drop
    if (n > OUT_LIMIT) { fprintf(stderr, "[net] refusing oversized %llu-byte output message\n", (unsigned long long)n); pthread_mutex_unlock(&qm); return; }
    while (outlen > OUT_LIMIT - n && client_fd >= 0) pthread_cond_wait(&qspace, &qm);   // back-pressure on a slow browser
    if (client_fd < 0) { pthread_mutex_unlock(&qm); return; }
    if (outlen + n > outcap) {
        size_t required = outlen + n, capacity = required > OUT_LIMIT / 2 ? OUT_LIMIT : required * 2;
        uint8_t *grown = realloc(outbuf, capacity);
        if (!grown) { fprintf(stderr, "[net] could not grow output queue to %llu bytes\n", (unsigned long long)capacity); pthread_mutex_unlock(&qm); return; }
        outbuf = grown; outcap = capacity;
    }
    memcpy(outbuf + outlen, data, n); outlen += n;
    pthread_cond_signal(&qc); pthread_mutex_unlock(&qm);
}

// ---- incoming input events (network thread -> emulation thread) -----------------------------------------------------------------------
#define INQ 1024
static NetInput inq[INQ]; static int inh, int_;
static pthread_mutex_t im = PTHREAD_MUTEX_INITIALIZER;
int net_poll_input(NetInput *out) {
    int r = 0; pthread_mutex_lock(&im); if (inh != int_) { *out = inq[inh]; inh = (inh + 1) % INQ; r = 1; } pthread_mutex_unlock(&im); return r;
}
volatile uint32_t g_net_ack = 0; volatile int g_net_ack_seen = 0;      // viewer flow control: type-9 messages carry the number of frames the page has presented
static void push_input(NetInput *e) { if (e->type == 9) { g_net_ack = (uint32_t)e->a; g_net_ack_seen = 1; return; } pthread_mutex_lock(&im); if (e->type == 15 && (int_ + 1) % INQ == inh) inh = int_ = 0; if ((int_ + 1) % INQ != inh) { inq[int_] = *e; int_ = (int_ + 1) % INQ; } pthread_mutex_unlock(&im); }
void net_release_all_input(void) { NetInput released = {0}; released.type = 15; push_input(&released); }

// ---- sockets ---------------------------------------------------------------------------------------------------------------------------------
static int write_all(int fd, const void *p, size_t n) { const uint8_t *b = p; while (n) { ssize_t w = write(fd, b, n); if (w <= 0) return -1; b += w; n -= (size_t)w; } return 0; }
static int writev_all(int fd, struct iovec *iov, int cnt) {          // header + payload in one syscall (no tiny stand-alone segment for the 4-10 byte WebSocket header)
    while (cnt) {
        ssize_t w = writev(fd, iov, cnt); if (w <= 0) return -1;
        while (cnt && (size_t)w >= iov->iov_len) { w -= (ssize_t)iov->iov_len; iov++; cnt--; }
        if (cnt) { iov->iov_base = (char *)iov->iov_base + w; iov->iov_len -= (size_t)w; }
    }
    return 0;
}
static int read_all(int fd, void *p, size_t n) { uint8_t *b = p; while (n) { ssize_t r = read(fd, b, n); if (r <= 0) return -1; b += r; n -= (size_t)r; } return 0; }
static void socket_shutdown(int fd) {
#ifdef _WIN32
    shutdown((SOCKET)fd, SD_BOTH);
#else
    shutdown(fd, SHUT_RDWR);
#endif
}

static void *writer_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    for (;;) {
        pthread_mutex_lock(&qm);
        while (outlen == 0 && client_fd == fd) pthread_cond_wait(&qc, &qm);
        if (client_fd != fd) { pthread_mutex_unlock(&qm); return NULL; }
        uint8_t *chunk = outbuf; size_t n = outlen; outbuf = NULL; outlen = outcap = 0;
        pthread_cond_broadcast(&qspace); pthread_mutex_unlock(&qm);
        uint8_t hdr[10]; int hl = 0; hdr[hl++] = 0x82;
        if (n < 126) hdr[hl++] = (uint8_t)n; else if (n < 65536) { hdr[hl++] = 126; hdr[hl++] = n >> 8; hdr[hl++] = (uint8_t)n; }
        else { hdr[hl++] = 127; for (int i = 7; i >= 0; i--) hdr[hl++] = (uint8_t)((uint64_t)n >> (8 * i)); }
        struct iovec iov[2] = {{hdr, (size_t)hl}, {chunk, n}}; int bad = writev_all(fd, iov, 2); free(chunk);
        if (bad) {
            pthread_mutex_lock(&qm);
            if (client_fd == fd) client_fd = -2;   // reserve the slot until conn_thread has joined us and closed fd
            pthread_cond_broadcast(&qspace); pthread_cond_broadcast(&qc); pthread_mutex_unlock(&qm);
            socket_shutdown(fd);                   // interrupt the reader; conn_thread remains the sole close owner
            return NULL;
        }
    }
}
static void http_empty(int fd, int status, const char *reason) {
    char response[160]; int n = snprintf(response, sizeof response, "HTTP/1.1 %d %s\r\nContent-Length: 0\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", status, reason);
    if (n > 0 && n < (int)sizeof response) write_all(fd, response, (size_t)n);
}
static FILE *open_regular_nofollow(const char *path, uint64_t *size) {
#ifdef _WIN32
    HANDLE handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (handle == INVALID_HANDLE_VALUE) return NULL;
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        CloseHandle(handle); return NULL;
    }
    *size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    int raw = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY);
    if (raw < 0) { CloseHandle(handle); return NULL; }
    FILE *file = _fdopen(raw, "rb");
    if (!file) { _close(raw); return NULL; }
    return file;
#else
    int raw = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (raw < 0) return NULL;
    struct stat st;
    if (fstat(raw, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) { close(raw); return NULL; }
    *size = (uint64_t)st.st_size;
    FILE *file = fdopen(raw, "rb");
    if (!file) { close(raw); return NULL; }
    return file;
#endif
}
static void serve_file(int fd, const char *path) {
    char fn[1024];
    int configured_input = !strcmp(path, "/input-config.json");
    if (configured_input) {
        const char *config = getenv("BLUR_INPUT_CONFIG");
        if (!config || !*config) { http_empty(fd, 404, "Not Found"); return; }
        if (snprintf(fn, sizeof fn, "%s", config) >= (int)sizeof fn) { http_empty(fd, 500, "Configured Path Too Long"); return; }
    } else if (snprintf(fn, sizeof fn, "web%s", strcmp(path, "/") ? path : "/index.html") >= (int)sizeof fn) { http_empty(fd, 414, "URI Too Long"); return; }
    if (strstr(fn, "..")) { http_empty(fd, 404, "Not Found"); return; }
    uint64_t n64 = 0, limit = configured_input ? (1u << 20) : (64u << 20);
    FILE *f = open_regular_nofollow(fn, &n64);
    if (!f) { http_empty(fd, configured_input ? 500 : 404, configured_input ? "Input Configuration Unreadable" : "Not Found"); return; }
    if (n64 > limit || n64 > SIZE_MAX) { fclose(f); http_empty(fd, 500, "File Too Large"); return; }
    size_t n = (size_t)n64;
    char *b = malloc(n ? n : 1);
    if (!b) { fclose(f); http_empty(fd, 500, "Out Of Memory"); return; }
    if (n && fread(b, 1, n, f) != n) { free(b); fclose(f); http_empty(fd, 500, "File Read Failed"); return; }
    fclose(f);
    const char *ct = strstr(fn, ".js") ? "text/javascript" : strstr(fn, ".html") ? "text/html" : strstr(fn, ".json") ? "application/json" : "application/octet-stream";
    char h[256]; int hl = snprintf(h, sizeof h, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %llu\r\nCache-Control: no-store\r\n\r\n", ct, (unsigned long long)n);
    if (hl > 0 && hl < (int)sizeof h) { write_all(fd, h, (size_t)hl); write_all(fd, b, n); }
    free(b);
}
static void *conn_thread(void *arg) {
    int fd = (int)(intptr_t)arg; char req[4096]; int n = 0;
    while (n < (int)sizeof req - 1) { ssize_t r = read(fd, req + n, 1); if (r <= 0) { close(fd); return NULL; } n++; if (n >= 4 && !memcmp(req + n - 4, "\r\n\r\n", 4)) break; }
    req[n] = 0; char path[256] = "/"; sscanf(req, "GET %255s", path); { char *q = strchr(path, '?'); if (q) *q = 0; }
    char *key = strcasestr(req, "Sec-WebSocket-Key:");
    if (!key) { serve_file(fd, path); close(fd); return NULL; }
    key += 18; while (*key == ' ') key++; char k[64] = {0}; for (int i = 0; i < 60 && key[i] > ' '; i++) k[i] = key[i];
    char cat[128]; snprintf(cat, sizeof cat, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", k);
    uint8_t dig[20]; sha1((uint8_t *)cat, strlen(cat), dig); char acc[40]; b64(dig, 20, acc);
    char resp[256]; int rl = snprintf(resp, sizeof resp, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
    write_all(fd, resp, rl);
    { int one = getenv("BLUR_NODELAY") ? atoi(getenv("BLUR_NODELAY")) : 1, sb = 4 << 20; if (one) { setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one); setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char *)&sb, sizeof sb); } }      // BLUR_NODELAY=0: old behaviour (A/B)     // no Nagle: the tail segment of a frame must not wait for the ACK of the previous ones
    pthread_mutex_lock(&qm);
    if (client_fd != -1) { pthread_mutex_unlock(&qm); static const uint8_t busy[4] = {0x88, 0x02, 0x03, 0xf5}; write_all(fd, (const char *)busy, 4); close(fd); return NULL; }   // first viewer wins: refuse with close code 1013 ("try again later")
    hc_viewer_reset(); client_fd = fd; g_net_epoch++; free(outbuf); outbuf = NULL; outlen = outcap = 0; pthread_mutex_unlock(&qm);
    fprintf(stderr, "[net] browser connected\n");
    pthread_t wt; int writer_started = pthread_create(&wt, NULL, writer_thread, (void *)(intptr_t)fd) == 0;
    if (!writer_started) fprintf(stderr, "[net] could not start browser writer thread\n");
    while (writer_started) {                                                       // read masked client frames: input events
        uint8_t h[2]; if (read_all(fd, h, 2)) break;
        uint64_t len = h[1] & 0x7f; if (len == 126) { uint8_t e[2]; if (read_all(fd, e, 2)) break; len = e[0] << 8 | e[1]; }
        else if (len == 127) { uint8_t e[8]; if (read_all(fd, e, 8)) break; len = 0; for (int i = 0; i < 8; i++) len = len << 8 | e[i]; }
        if (!(h[1] & 0x80) || !(h[0] & 0x80)) break;
        uint8_t mask[4]; if (read_all(fd, mask, 4)) break;
        if (len > (1u << 20)) break;
        uint8_t *pl = malloc((size_t)len + 1); if (!pl || read_all(fd, pl, (size_t)len)) { free(pl); break; }
        for (uint64_t i = 0; i < len; i++) pl[i] ^= mask[i & 3];
        uint32_t magic = 0; if (len >= sizeof magic) memcpy(&magic, pl, sizeof magic);
        if ((h[0] & 0xf) == 2 && len >= 12 && (magic == 0x48430Au || magic == 0x48430Cu)) hc_net_msg(pl, len);      // page cache announce (0x48430A) / stored-ack (0x48430C): variable length, not a NetInput
        else if ((h[0] & 0xf) == 2 && len == sizeof(NetInput)) { NetInput e; memcpy(&e, pl, sizeof e); push_input(&e); }
        free(pl); if ((h[0] & 0xf) == 8) break;
    }
    net_release_all_input();       // an abrupt tab/network loss must not leave host keys, mouse buttons or XInput held
    pthread_mutex_lock(&qm);
    if (client_fd == fd) client_fd = -2;
    pthread_cond_broadcast(&qspace); pthread_cond_broadcast(&qc); pthread_mutex_unlock(&qm);
    socket_shutdown(fd);                       // unblock a writer in send/WSASend before joining it
    if (writer_started) pthread_join(wt, NULL);
    close(fd);                                  // conn_thread is the sole owner that closes WebSocket fds
    pthread_mutex_lock(&qm);
    if (client_fd == -2) client_fd = -1;
    pthread_cond_broadcast(&qspace); pthread_cond_broadcast(&qc); pthread_mutex_unlock(&qm);
    fprintf(stderr, "[net] browser disconnected\n"); return NULL;
}
static void *accept_thread(void *arg) {
    int ls = (int)(intptr_t)arg;
    for (;;) { int fd = accept(ls, NULL, NULL); if (fd < 0) continue; pthread_t t; if (pthread_create(&t, NULL, conn_thread, (void *)(intptr_t)fd)) { close(fd); continue; } pthread_detach(t); }
    return NULL;
}
int net_start(int port) {
#ifdef _WIN32
    { WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd); }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    // a viewer that vanishes mid-write (killed browser, dead tab) must not kill the game with SIGPIPE

    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) { perror("net socket"); return -1; }
#ifndef _WIN32
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);      // (on Windows SO_REUSEADDR would allow port hijacking: leave it off)
#endif
   
    // BLUR_BIND=0.0.0.0 (or a specific interface address) serves other machines on the LAN; the default stays loopback only. Anyone who can reach the port controls the game.
    const char *bind_ip = getenv("BLUR_BIND"); struct in_addr ba; ba.s_addr = htonl(INADDR_LOOPBACK); if (bind_ip && *bind_ip && inet_pton(AF_INET, bind_ip, &ba) != 1) { fprintf(stderr, "[net] BLUR_BIND=%s is not an IPv4 address, using 127.0.0.1\n", bind_ip); ba.s_addr = htonl(INADDR_LOOPBACK); }
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port); a.sin_addr = ba;
    if (bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, 4)) { perror("net bind"); close(ls); return -1; }
    pthread_t t;
    if (pthread_create(&t, NULL, accept_thread, (void *)(intptr_t)ls)) { close(ls); fprintf(stderr, "[net] could not start accept thread\n"); return -1; }
    server_up = 1; pthread_detach(t);
    if (ba.s_addr == htonl(INADDR_LOOPBACK)) fprintf(stderr, "[net] open http://127.0.0.1:%d/ in a browser\n", port);
    else { char ip[64]; inet_ntop(AF_INET, &ba, ip, sizeof ip); fprintf(stderr, "[net] listening on %s:%d (LAN): open http://<this-machine-ip>:%d/ in a browser (secure-context features need localhost or https, see PLAN_120FPS.md F156)\n", ip, port, port); }
    return 0;
}
