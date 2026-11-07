#!/bin/bash
set -euo pipefail
# racedrive.sh - reach an actual race headlessly in one command (about 2 minutes into the run: ~120 s, 2.4-3.1k draws/frame at 60 fps).
# usage: tools/shot/racedrive.sh <tag> <url-hash> <seconds> [extra host env VAR=val ...]
#   e.g. tools/shot/racedrive.sh t1 "hcache=0" 230          (cache off)        tools/shot/racedrive.sh t2 "hcache=1" 230 BLUR_HCSTAT=1
# env: BIN=<blurrt path relative to port/harness> (default ./blurrt)   PORT (default 8792; never use 8080/8081/8765)   PROF=<chromium profile dir> (keep it to reuse the page's IndexedDB cache; default /tmp/blur-racedrive/prof_<tag>)
#      PREWARM_SECONDS=<seconds before attaching the browser> (default 0). A positive value starts the host without waiting for a viewer and disables transport pacing only during this private bootstrap.
#      DRIVER=<page script> (default racedrive.js; livetest.js = renderer comparison with console/memory/stats capture)
#      WORK=<scratch dir> (default /tmp/blur-racedrive): save copy, logs <tag>.host.log / <tag>.page.log, shots/<tag>.*.png
# how the keys are split: the HOST drives title, account skip, warning and
# single-player selection with frame-relative Lua events (SEEDACCT=0);
#   the PAGE (racedrive.js, puppeteer keyboard) starts pressing Enter every 8 s after 45 s: warning Ok, single player, career movie (~9 GB of Bink frames), career menus, race.
# Uses a fresh copy of port/harness/save per tag. Kills only the game it started.
TAG=${1:?tag}; FRAG=${2:-}; SECS=${3:-230}; shift 3
HERE=$(cd "$(dirname "$0")" && pwd); H=$HERE/../../port/harness
WORK=${WORK:-${XDG_CACHE_HOME:-$HOME/.cache}/blur-web/racedrive}; PORT=${PORT:-8792}; mkdir -p "$WORK/shots"
PREWARM_SECONDS=${PREWARM_SECONDS:-0}
command -v node >/dev/null || { echo "node is required" >&2; exit 1; }
(cd "$HERE" && node -e 'require("puppeteer-core")') >/dev/null 2>&1 || {
  echo "puppeteer-core is missing; run 'bun install --frozen-lockfile' in tools/shot" >&2
  exit 1
}
CHROMIUM=${CHROMIUM:-$(command -v chromium || command -v microsoft-edge || true)}
[ -x "$CHROMIUM" ] || { echo "CHROMIUM must name an executable Chromium-family browser" >&2; exit 1; }
BIN=${BIN:-./blurrt}
[ -x "$H/$BIN" ] || { echo "host binary is not executable: $H/$BIN" >&2; exit 1; }
echo "host binary: $H/$BIN"
case "$SECS" in *[!0-9]*|'') echo "seconds must be a positive integer" >&2; exit 1;; esac
[ "$SECS" -gt 0 ] || { echo "seconds must be a positive integer" >&2; exit 1; }
case "$PREWARM_SECONDS" in *[!0-9]*|'') echo "PREWARM_SECONDS must be a non-negative integer" >&2; exit 1;; esac
[ -d "$WORK/save_$TAG/userprofile" ] || {
  mkdir -p "$WORK/save_$TAG"
  [ ! -d "$H/save" ] || cp -R "$H/save/." "$WORK/save_$TAG/"
}
K="Enter@45,Enter@L:StopScoreboardRequests+60,Down@L:ReadOnlineAccountDetails+150,Enter@L:ReadOnlineAccountDetails+175,Enter@L:ReadOnlineAccountDetails+260,Enter@L:ReadOnlineAccountDetails+340,Enter@L:ReadOnlineAccountDetails+440"
PREWARM_ENV=()
[ "$PREWARM_SECONDS" -eq 0 ] || PREWARM_ENV=(BLUR_NOWAIT=1 BLUR_PREWARM=1)
(
  cd "$H"
  exec env -u BLUR_NOWAIT -u BLUR_PREWARM BLUR_PAR=1 BLUR_RECOMP=1 BLUR_WEB="$PORT" BLUR_SEEDACCT=0 BLUR_SAVEDIR="$WORK/save_$TAG" BLUR_KEYS="$K" "${PREWARM_ENV[@]}" "$@" "$BIN" ../../game/extracted/BLUR/Blur.exe
) > "$WORK/$TAG.host.log" 2>&1 &
HP=$!
cleanup() {
  trap - EXIT INT TERM
  kill -INT "$HP" 2>/dev/null || true
  timeout 10s tail --pid="$HP" -f /dev/null >/dev/null 2>&1 || kill -KILL "$HP" 2>/dev/null || true
  wait "$HP" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
sleep "$((PREWARM_SECONDS + 4))"
cd "$HERE" && HYB=45 PROF=${PROF:-$WORK/prof_$TAG} node "${DRIVER:-racedrive.js}" "http://127.0.0.1:$PORT/#$FRAG" "$SECS" "$WORK/shots/$TAG" > "$WORK/$TAG.page.log" 2>&1
cleanup
grep " S " "$WORK/$TAG.page.log" | tail -5
