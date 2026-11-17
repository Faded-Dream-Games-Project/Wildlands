#!/usr/bin/env bash
# Reproduce the "textures turn white / untextured after camera movement"
# bug: render the same final camera pose after (a) a static run and
# (b) a 360-degree rotating run, with the depth-clear fix disabled
# (WL_NO_DEPTHCLEAR=1, the pre-fix behaviour) and enabled, then pixel-diff.
#
# With the bug, the rotating run accumulates stale depth-buffer values and
# terrain fragments farther than the stale depth are discarded -> holes
# that show the pale sky ("white blocks"). With the fix the two runs must
# match to within noise (cloud drift / time-of-day).
set -u
cd "$(dirname "$0")/.."

OUT=${1:-/tmp/render-repro}
mkdir -p "$OUT"
WORLD=ReproWorld
SEED=20261117
FRAMES=240
SPIN=1.5   # deg/frame * 240 frames = one full 360-degree turn

run() { # run <tag> <extra-args...>
    local tag=$1; shift
    tools/launch.sh ./bin/wildlands \
        --world "$WORLD" --seed "$SEED" --frames $FRAMES \
        --screenshot "$OUT/$tag.png" "$@" >/dev/null 2>&1
    echo "captured $tag: $(ls -la "$OUT/$tag.png" 2>/dev/null | awk '{print $5}') bytes"
}

# 1. create the world once so every run starts from the same saved session
tools/launch.sh ./bin/wildlands --world "$WORLD" --seed "$SEED" \
    --frames 40 >/dev/null 2>&1

# 2. static run with the old (buggy) depth behaviour: baseline at pose P
WL_NO_DEPTHCLEAR=1 run static_bug
# 3. rotating run, old behaviour: ends at the same pose P with stale depth
WL_NO_DEPTHCLEAR=1 run spin_bug --lookspin $SPIN
# 4. rotating run with the fix: same pose P, depth cleared every frame
run spin_fixed --lookspin $SPIN
# 5. static run with the fix: noise floor for the comparison
run static_fixed

echo "=== pixel comparison ==="
python3 - "$OUT" <<'EOF'
import sys
from PIL import Image, ImageChops
import numpy as np

out = sys.argv[1]
def load(t):
    return np.asarray(Image.open(f"{out}/{t}.png").convert("RGB"), dtype=np.int16)

def diff_frac(a, b, thresh=24):
    a, b = load(a), load(b)
    d = np.abs(a - b).max(axis=2)
    return float((d > thresh).mean())

pairs = [
    ("static_bug", "spin_bug",   "stale depth (BUG):   static vs rotated"),
    ("static_bug", "spin_fixed", "depth clear (FIXED): static vs rotated"),
    ("static_bug", "static_fixed","noise floor:         static vs static"),
]
for a, b, label in pairs:
    print(f"{label:46s} changed-pixel fraction = {diff_frac(a,b)*100:6.2f}%")
EOF
