#!/usr/bin/env bash
# Genuine-input regression tests for the pause menu, inventory overlay and
# camera stability. Runs the real game under Xvfb and drives it with real
# X11 key events (tools/xsend), so raylib's actual key-edge handling and
# raylib's cursor warp behaviour are exercised — the same paths a physical
# keyboard takes.
#
# Checks:
#   1. ESC opens the pause menu and it STAYS open (screenshot 1.2s later).
#   2. ESC resumes; E opens the inventory and it STAYS open.
#   3. E closes the inventory again.
#   4. Camera yaw/pitch are unchanged by the whole open/close sequence
#      (cursor moved inside the menus — the classic warp-jump path).
#   5. Block-item icons render (Oak Log et al.) in a kit inventory.
set -u
cd "$(dirname "$0")/.."

OUT=${1:-/tmp/input-test}
mkdir -p "$OUT"
XVFB_DISP=:98
GAME_LOG=""
XVFB_PID=""

cleanup() {
    [ -n "${GAME_PID:-}" ] && kill "$GAME_PID" 2>/dev/null
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
}
trap cleanup EXIT

# build the injector if needed
if [ ! -x tools/xsend ]; then
    say "building tools/xsend"
    cc -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L \
       -Ithird_party/sysroot/usr/include -o tools/xsend tools/xsend.c \
       -Lthird_party/sysroot/usr/lib/x86_64-linux-gnu -lX11 || exit 1
fi

fail=0
say() { echo "[input-test] $*"; }
check() { # check <name> <ok>
    if [ "$2" = "0" ]; then say "PASS $1"; else say "FAIL $1"; fail=1; fi
}

latest_log() { ls -t logs/wildlands-*.log | head -1; }
last_yaw_pitch() { # prints "yaw pitch" from the last demo diagnostics line
    grep "demo pos" "$1" | tail -1 | sed 's/.*yaw \([-0-9.]*\) pitch \([-0-9.]*\).*/\1 \2/'
}
newest_shot() { ls -t screenshots/*.png 2>/dev/null | head -1; }

# ---------------------------------------------------------------- run 1
say "starting Xvfb $XVFB_DISP"
Xvfb $XVFB_DISP -screen 0 1280x720x24 2>/dev/null &
XVFB_PID=$!
for _ in $(seq 1 30); do [ -S /tmp/.X11-unix/X98 ] && break; sleep 0.1; done

rm -rf worlds/InputTest; find screenshots -name '*.png' ! -name 'README.md-screenshot.png' -delete
DISPLAY=$XVFB_DISP LIBGL_ALWAYS_SOFTWARE=1 ./bin/wildlands \
    --world InputTest --seed 777 --frames 2400 >"$OUT/game.out" 2>&1 &
GAME_PID=$!

# focus the window as soon as it exists (the game auto-pauses when
# unfocused, and no WM runs under Xvfb to give it focus)
for _ in $(seq 1 100); do
    ./tools/xsend $XVFB_DISP Wildlands move 640 360 >/dev/null 2>&1 && break
    sleep 0.2
done
say "window focused"

# wait until gameplay is running
GAME_LOG=""
for _ in $(seq 1 300); do
    GAME_LOG=$(latest_log)
    [ -n "$GAME_LOG" ] && grep -q "demo pos" "$GAME_LOG" && break
    sleep 0.2
done
[ -n "$GAME_LOG" ] && grep -q "demo pos" "$GAME_LOG"
check "game reaches playing state" $?

BASE=$(last_yaw_pitch "$GAME_LOG")
say "baseline yaw/pitch: $BASE"

# 1. ESC opens the pause menu; screenshot 1.2s later must still show it
./tools/xsend $XVFB_DISP Wildlands key Escape 80
sleep 1.2
./tools/xsend $XVFB_DISP Wildlands key F2 60
sleep 0.8
PAUSE_SHOT=$(newest_shot)
cp "$PAUSE_SHOT" "$OUT/pause-open.png"

# 2. ESC resumes, E opens the inventory; move the cursor far from center
#    (this is the cursor-warp path that used to snap the camera)
./tools/xsend $XVFB_DISP Wildlands key Escape 80
sleep 0.6
./tools/xsend $XVFB_DISP Wildlands key e 80
sleep 0.8
./tools/xsend $XVFB_DISP Wildlands move 300 200
sleep 0.3
./tools/xsend $XVFB_DISP Wildlands key F2 60
sleep 0.8
INV_SHOT=$(newest_shot)
cp "$INV_SHOT" "$OUT/inventory-open.png"

# 3. E closes the inventory
./tools/xsend $XVFB_DISP Wildlands key e 80
sleep 0.8

# 4. camera stability across the whole sequence
AFTER=$(last_yaw_pitch "$GAME_LOG")
say "final yaw/pitch:    $AFTER"
python3 - "$BASE" "$AFTER" <<'EOF'
import sys
b = [float(x) for x in sys.argv[1].split()]
a = [float(x) for x in sys.argv[2].split()]
dyaw = min(abs(a[0]-b[0]), abs(abs(a[0]-b[0])-6.28318))  # wrap-aware
dp = abs(a[1]-b[1])
sys.exit(0 if dyaw < 0.05 and dp < 0.05 else 1)
EOF
check "camera direction unchanged by menu open/close" $?

kill $GAME_PID 2>/dev/null; wait $GAME_PID 2>/dev/null; GAME_PID=""

# ---------------------------------------------------------------- checks
python3 - "$OUT" <<'EOF'
import sys
from PIL import Image
import numpy as np
out = sys.argv[1]

def band_std(path, y0, y1, x0f=0.25, x1f=0.75):
    a = np.asarray(Image.open(path).convert("L"), dtype=np.float32)
    h, w = a.shape
    return a[int(h*y0):int(h*y1), int(w*x0f):int(w*x1f)]

# pause menu: "Game Paused" title band + panel below it must have content
pause = band_std(f"{out}/pause-open.png", 0.14, 0.20)
panel = band_std(f"{out}/pause-open.png", 0.34, 0.52)
ok_pause = panel.std() > 12 and pause.std() > 8
print(f"  pause: title-std={pause.std():.1f} panel-std={panel.std():.1f}")
sys.exit(0 if ok_pause else 1)
EOF
check "pause menu visible 1.2s after ESC (stays open)" $?

python3 - "$OUT" <<'EOF'
import sys
from PIL import Image
import numpy as np
out = sys.argv[1]
a = np.asarray(Image.open(f"{out}/inventory-open.png").convert("L"), dtype=np.float32)
h, w = a.shape
panel = a[int(h*0.25):int(h*0.75), int(w*0.30):int(w*0.70)]
# the inventory panel + 40 slot frames produce strong structure
ok = panel.std() > 14
print(f"  inventory: panel-std={panel.std():.1f}")
sys.exit(0 if ok else 1)
EOF
check "inventory overlay visible after E (stays open)" $?

# ---------------------------------------------------------------- run 2
say "kit run: icons + held-item switching with real number keys"
rm -rf worlds/IconKit; find screenshots -name '*.png' ! -name 'README.md-screenshot.png' -delete
DISPLAY=$XVFB_DISP LIBGL_ALWAYS_SOFTWARE=1 ./bin/wildlands \
    --world IconKit --seed 42 --frames 4000 --overlay inv >"$OUT/kit.out" 2>&1 &
GAME_PID=$!
for _ in $(seq 1 100); do
    ./tools/xsend $XVFB_DISP Wildlands move 640 360 >/dev/null 2>&1 && break
    sleep 0.2
done
sleep 6                                           # world entry + overlay up
./tools/xsend $XVFB_DISP Wildlands key F2 60      # icons (overlay open)
sleep 0.8
./tools/xsend $XVFB_DISP Wildlands key e 80       # close inventory
sleep 0.5
./tools/xsend $XVFB_DISP Wildlands key 6 80       # select slot 5 = apple
sleep 0.5
./tools/xsend $XVFB_DISP Wildlands key F2 60      # held apple in view
sleep 0.8
kill $GAME_PID 2>/dev/null; wait $GAME_PID 2>/dev/null; GAME_PID=""

mapfile -t SHOTS < <(ls -t screenshots/*.png 2>/dev/null)
python3 - "$OUT" "${SHOTS[1]}" "${SHOTS[0]}" <<'PYCHK'
import sys
from PIL import Image
import numpy as np
out, kit, held = sys.argv[1], sys.argv[2], sys.argv[3]
a = np.asarray(Image.open(kit).convert("RGB"), dtype=np.int16)
r,g,b = a[:,:,0],a[:,:,1],a[:,:,2]
def brown(s): return ((r>80)&(r<200)&(g>50)&(g<r-15)&(b<g-10)&(b<r-30))[s].sum()
log_px  = brown((slice(480,513), slice(485,518)))   # Oak Log icon cell
grasspx = ((g>90)&(g>r+25)&(g>b+25))[slice(480,513), slice(439,472)].sum()
print(f"  icons: log-brown={log_px} grass-green={grasspx}")
icons_ok = log_px > 100 and grasspx > 100

h = np.asarray(Image.open(held).convert("RGB"), dtype=np.int16)
hr,hg,hb = h[:,:,0],h[:,:,1],h[:,:,2]
region = (slice(int(h.shape[0]*0.52), int(h.shape[0]*0.96)),
          slice(int(h.shape[1]*0.55), int(h.shape[1]*0.97)))
red = ((hr>110)&(hr<230)&(hg<hr-40)&(hb<hr-40))[region].sum()
print(f"  held apple: red px={red}")
held_ok = red > 150
sys.exit(0 if icons_ok and held_ok else 1)
PYCHK
check "Oak Log/grass icons render; number key switches held item" $?

# ---------------------------------------------------------------- run 3
say "mining run: real mouse click collects a visible hotbar item"
rm -rf worlds/MineTest; find screenshots -name '*.png' ! -name 'README.md-screenshot.png' -delete
DISPLAY=$XVFB_DISP LIBGL_ALWAYS_SOFTWARE=1 ./bin/wildlands \
    --world MineTest --seed 123 --frames 4000 --pitch -60 >"$OUT/mine.out" 2>&1 &
GAME_PID=$!
for _ in $(seq 1 100); do
    ./tools/xsend $XVFB_DISP Wildlands move 640 360 >/dev/null 2>&1 && break
    sleep 0.2
done
MINE_LOG=$(latest_log)
for _ in $(seq 1 300); do
    MINE_LOG=$(latest_log)
    [ -n "$MINE_LOG" ] && grep -q "demo pos" "$MINE_LOG" 2>/dev/null && break
    sleep 0.2
done
sleep 1                                           # settle after entry
./tools/xsend $XVFB_DISP Wildlands key F2 60      # baseline: empty hotbar
sleep 0.8
./tools/xsend $XVFB_DISP Wildlands click left 1500 # mine the targeted block (0.6s break time + margin)
sleep 1.0
./tools/xsend $XVFB_DISP Wildlands key F2 60      # hotbar should show the drop
sleep 0.8
kill $GAME_PID 2>/dev/null; wait $GAME_PID 2>/dev/null; GAME_PID=""

mapfile -t SHOTS < <(ls -t screenshots/*.png 2>/dev/null)
python3 - "$OUT" "${SHOTS[1]}" "${SHOTS[0]}" <<'PYCHK'
import sys
from PIL import Image
import numpy as np
out, before, after = sys.argv[1], sys.argv[2], sys.argv[3]

def hotbar_cell0(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    h, w, _ = a.shape
    # HUD hotbar: 9 cells of 44px centered at the bottom
    cell_w = 44.0
    x0 = (w - cell_w * 9) / 2.0
    y0 = h - cell_w - 12.0
    cell = a[int(y0)+6:int(y0)+40, int(x0)+5:int(x0)+39]
    return cell.std(), cell.mean()

s0, m0 = hotbar_cell0(before)
s1, m1 = hotbar_cell0(after)
print(f"  hotbar cell0 std: {s0:.1f} -> {s1:.1f} (mean {m0:.0f} -> {m1:.0f})")
# an icon + count text appear in the cell after mining
sys.exit(0 if s1 > s0 + 8 else 1)
PYCHK
check "mined block appears as a visible hotbar item" $?

# ---------------------------------------------------------------- run 4
say "night lighting: place a torch in the dark via real input"
rm -rf worlds/TorchNight
find screenshots -name '*.png' ! -name 'README.md-screenshot.png' -delete 2>/dev/null
DISPLAY=$XVFB_DISP LIBGL_ALWAYS_SOFTWARE=1 ./bin/wildlands \
    --world TorchNight --seed 3 --frames 4000 --time 15000 \
    --overlay inv --pitch -25 >"$OUT/torch.out" 2>&1 &
GAME_PID=$!
for _ in $(seq 1 100); do
    ./tools/xsend $XVFB_DISP Wildlands move 640 360 >/dev/null 2>&1 && break
    sleep 0.2
done
TLOG=$(latest_log)
for _ in $(seq 1 300); do
    TLOG=$(latest_log)
    [ -n "$TLOG" ] && grep -q "demo pos" "$TLOG" 2>/dev/null && break
    sleep 0.2
done
sleep 1
./tools/xsend $XVFB_DISP Wildlands key e 80       # close inventory
sleep 0.6
./tools/xsend $XVFB_DISP Wildlands key 8 80       # slot 7 = torch
sleep 0.6
./tools/xsend $XVFB_DISP Wildlands click right 200 # place the torch
sleep 3
./tools/xsend $XVFB_DISP Wildlands key F2 60
sleep 1.2
kill $GAME_PID 2>/dev/null; wait $GAME_PID 2>/dev/null; GAME_PID=""

TORCH_SHOT=$(ls -t screenshots/*.png 2>/dev/null | head -1)
python3 - "$TORCH_SHOT" <<'PYCHK'
import sys
from PIL import Image
import numpy as np
a = np.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=np.float32)
lum = a.mean(axis=2)
r, g, b = a[:,:,0], a[:,:,1], a[:,:,2]
scene = lum[:640]
rs, gs, bs = r[:640], g[:640], b[:640]
warm = ((rs>90)&(rs<255)&(rs>gs+10)&(gs>bs)).sum()
print(f"  torch pool: warm px={warm} scene mean={scene.mean():.0f}")
sys.exit(0 if warm > 3000 else 1)
PYCHK
check "placed torch illuminates the night scene" $?

# ---------------------------------------------------------------- run 5
say "furnace: place it, open the UI, smelt with real clicks"
find screenshots -name '*.png' ! -name 'README.md-screenshot.png' -delete 2>/dev/null
rm -rf worlds/FurnUITest
DISPLAY=$XVFB_DISP LIBGL_ALWAYS_SOFTWARE=1 ./bin/wildlands \
    --world FurnUITest --seed 3 --frames 4000 --overlay inv --pitch -30 \
    >"$OUT/furn.out" 2>&1 &
GAME_PID=$!
for _ in $(seq 1 100); do
    ./tools/xsend $XVFB_DISP Wildlands move 640 360 >/dev/null 2>&1 && break
    sleep 0.2
done
FLOG=$(latest_log)
for _ in $(seq 1 300); do
    FLOG=$(latest_log)
    [ -n "$FLOG" ] && grep -q "demo pos" "$FLOG" 2>/dev/null && break
    sleep 0.2
done
sleep 1
./tools/xsend $XVFB_DISP Wildlands key e 80       # close inventory
sleep 0.6
./tools/xsend $XVFB_DISP Wildlands key 9 80       # slot 8 = furnace
sleep 0.6
./tools/xsend $XVFB_DISP Wildlands click right 200 # place the furnace
sleep 2
./tools/xsend $XVFB_DISP Wildlands click right 200 # open the furnace UI
sleep 1.5
./tools/xsend $XVFB_DISP Wildlands key F2 60      # screenshot the UI
sleep 1
kill $GAME_PID 2>/dev/null; wait $GAME_PID 2>/dev/null; GAME_PID=""

FURN_SHOT=$(ls -t screenshots/*.png 2>/dev/null | head -1)
python3 - "$FURN_SHOT" <<'PYCHK'
import sys
from PIL import Image
import numpy as np
a = np.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=np.float32)
h, w, _ = a.shape
# furnace panel: centered, with the title band and slot structure
panel = a[int(h*0.30):int(h*0.70), int(w*0.35):int(w*0.65)]
ok = panel.std() > 14
print(f"  furnace UI: panel-std={panel.std():.1f}")
sys.exit(0 if ok else 1)
PYCHK
check "furnace placed and its UI opens on right-click" $?

# ---------------------------------------------------------------- run 6
say "Q-drop: drop the selected item into the world"
find screenshots -name '*.png' ! -name 'README.md-screenshot.png' -delete 2>/dev/null
rm -rf worlds/QDropTest
DISPLAY=$XVFB_DISP LIBGL_ALWAYS_SOFTWARE=1 ./bin/wildlands \
    --world QDropTest --seed 3 --frames 4000 --overlay inv --pitch -20 \
    >"$OUT/qdrop.out" 2>&1 &
GAME_PID=$!
for _ in $(seq 1 100); do
    ./tools/xsend $XVFB_DISP Wildlands move 640 360 >/dev/null 2>&1 && break
    sleep 0.2
done
QLOG=$(latest_log)
for _ in $(seq 1 300); do
    QLOG=$(latest_log)
    [ -n "$QLOG" ] && grep -q "demo pos" "$QLOG" 2>/dev/null && break
    sleep 0.2
done
sleep 1
./tools/xsend $XVFB_DISP Wildlands key e 80       # close inventory
sleep 0.6
./tools/xsend $XVFB_DISP Wildlands key 2 80       # slot 1 = Oak Log x12
sleep 0.6
./tools/xsend $XVFB_DISP Wildlands key q 80       # drop the stack
sleep 1.5
./tools/xsend $XVFB_DISP Wildlands key F2 60      # the logs lie in the world
sleep 1
kill $GAME_PID 2>/dev/null; wait $GAME_PID 2>/dev/null; GAME_PID=""

Q_SHOT=$(ls -t screenshots/*.png 2>/dev/null | head -1)
python3 - "$Q_SHOT" <<'PYCHK'
import sys
from PIL import Image
import numpy as np
a = np.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=np.float32)
r, g, b = a[:,:,0], a[:,:,1], a[:,:,2]
# dropped oak logs: warm wood colors in the world (not the HUD strip)
brown = ((r>90)&(r<210)&(g>60)&(g<r-10)&(b<g-8)&(b<r-25))[:640]
print(f"  dropped logs: wood px={brown.sum()}")
sys.exit(0 if brown.sum() > 400 else 1)
PYCHK
check "Q drops the selected stack into the world" $?

say "done"
exit $fail
