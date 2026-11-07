// binkw32 replacement (FFmpeg). Handlers receive the guest stdcall arguments and return the value for eax.
#pragma once
#include <stdint.h>
typedef struct {
    uint32_t (*alloc)(uint32_t size);                       // guest allocation
    int (*map_path)(const char *guest_name, char *host);    // guest path -> host path; 0 if it cannot be mapped
    uint64_t (*now_us)(void);                               // monotonic microseconds
} BkHost;

#define BK_FLAG_WILL_LOOP 0x80u
static inline uint32_t bk_next_frame_number(uint32_t frame, uint32_t frames, uint32_t flags) {
    if (frame < frames) return frame + 1;
    return (flags & BK_FLAG_WILL_LOOP) != 0 ? 1 : frames;
}

void bk_set_host(const BkHost *h);
uint32_t bk_BinkOpen(const uint32_t *a), bk_BinkClose(const uint32_t *a), bk_BinkGetFrameBuffersInfo(const uint32_t *a), bk_BinkRegisterFrameBuffers(const uint32_t *a),
    bk_BinkDoFrame(const uint32_t *a), bk_BinkNextFrame(const uint32_t *a), bk_BinkWait(const uint32_t *a), bk_BinkShouldSkip(const uint32_t *a), bk_BinkPause(const uint32_t *a),
    bk_BinkGetRects(const uint32_t *a), bk_BinkSetVolume(const uint32_t *a), bk_BinkOpenTrack(const uint32_t *a), bk_BinkGetTrackData(const uint32_t *a),
    bk_BinkGetTrackID(const uint32_t *a), bk_BinkCloseTrack(const uint32_t *a);
