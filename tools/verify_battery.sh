#!/usr/bin/env bash
# Full verification battery for the depth-clear fix (Part 1) — every
# scenario runs twice: with the fix (normal) and with the pre-fix
# behaviour (WL_NO_DEPTHCLEAR=1). Each pair starts from the same saved
# session (player.dat removed so both runs settle identically), and the
# final frames are compared with a terrain-detail metric: with the bug,
# mid-distance terrain collapses into a flat pale wash (holes); with the
# fix it must match the static baseline.
set -u
cd "$(dirname "$0")/.."

OUT=${1:-/tmp/render-battery}
WORLD=BatteryWorld
SEED=31415926
mkdir -p "$OUT"

run() { # run <tag> <env> <frames> <args...>
    local tag=$1 envs=$2 frames=$3; shift 3
    rm -f "worlds/$WORLD/player.dat"   # identical start pose for each pair
    env $envs tools/launch.sh ./bin/wildlands \
        --world "$WORLD" --seed "$SEED" --frames "$frames" \
        --screenshot "$OUT/$tag.png" "$@" >/dev/null 2>&1
    [ -s "$OUT/$tag.png" ] && echo "  $tag ok" || echo "  $tag MISSING"
}

metric() { python3 - "$OUT/$1.png" <<'EOF'
import sys
from PIL import Image
import numpy as np
a = np.asarray(Image.open(sys.argv[1]).convert("L"), dtype=np.float32)
h, w = a.shape
mid, lower = a[int(h*0.45):int(h*0.60), :], a[int(h*0.70):int(h*0.95), :]
flat = (np.abs(np.diff(mid, axis=1)) < 2).mean()
print(f"{mid.std():5.1f} {flat*100:5.1f}% {lower.std():5.1f}")
EOF
}

echo "== create world =="
tools/launch.sh ./bin/wildlands --world "$WORLD" --seed "$SEED" --frames 40 >/dev/null 2>&1

echo "== scenarios (detail metrics: mid-std / mid-flat% / lower-std; bug collapses mid-std) =="
for scen in "stationary:240::" \
            "slow_rotate:240:--lookspin 0.25" \
            "fast_rotate:60:--lookspin 6" \
            "look_updown:240:--looknod 15" \
            "sky_and_back:240:--looknod 45" \
            "walk_rotate:240:--walk --lookspin 0.8" ; do
    name=${scen%%:*}; rest=${scen#*:}; frames=${rest%%:*}; args=${rest#*:}
    run "${name}_bug" "WL_NO_DEPTHCLEAR=1" "$frames" $args
    run "${name}_fix" "X=1"                 "$frames" $args
    printf "  %-14s bug: %s   fix: %s\n" "$name" "$(metric ${name}_bug)" "$(metric ${name}_fix)"
done

echo "== re-enter saved world, then rotate =="
run reenter_bug "WL_NO_DEPTHCLEAR=1" 240 --lookspin 1.5
run reenter_fix "X=1"                 240 --lookspin 1.5
printf "  %-14s bug: %s   fix: %s\n" "reenter+spin" "$(metric reenter_bug)" "$(metric reenter_fix)"

echo "== GL error trace on a rotating run (expect no errors) =="
WL_GL_TRACE=1 tools/launch.sh ./bin/wildlands --world "$WORLD" --frames 120 \
    --lookspin 3 --screenshot "$OUT/gltrace.png" >/dev/null 2>&1
LOG=$(ls -t logs/wildlands-*.log | head -1)
ERRS=$(grep -c "GL error" "$LOG" || true)
echo "  GL errors logged: $ERRS (GPU line: $(grep -o 'GPU: .*' "$LOG" | head -1))"

echo "== battery done: $OUT =="
