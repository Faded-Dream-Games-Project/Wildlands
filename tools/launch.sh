#!/usr/bin/env bash
# Launch Wildlands with a headless fallback: when no display is available
# and Xvfb is installed, start a virtual display with software GL so the
# game (and the test suite) still runs. Usage: tools/launch.sh <bin> [args]
set -u
BIN="${1:?usage: launch.sh <binary> [args...]}"
shift

if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
    exec "$BIN" "$@"
fi

if ! command -v Xvfb >/dev/null 2>&1; then
    cat >&2 <<'EOF'
Wildlands: FATAL: no display available (DISPLAY is not set) and Xvfb is
not installed. Either run the game from an X11/Wayland session, or install
Xvfb for headless testing (e.g. apt install xvfb).
EOF
    exit 1
fi

echo "Wildlands: no display found - starting Xvfb :99 (software GL)"
Xvfb :99 -screen 0 1280x720x24 2>/dev/null &
XPID=$!
for _ in $(seq 1 30); do
    [ -S /tmp/.X11-unix/X99 ] && break
    sleep 0.1
done
DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 "$BIN" "$@"
RC=$?
kill "$XPID" 2>/dev/null
exit $RC
