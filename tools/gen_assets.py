#!/usr/bin/env python3
"""Wildlands dev-time asset generator (NOT part of the game binary).

Generates every original asset as external files:
  assets/textures/blocks.png     block/item atlas (layout MUST match include/wl/tiles.h)
  assets/gui/*.png               interface textures
  assets/environment/*.png       sky bodies, clouds
  assets/audio/*.wav             sound effects
  assets/music/*.wav             ambient music loops
  mods/example_mod/assets/textures/*.png   example mod textures

Deterministic: fixed seeds everywhere, so re-running reproduces identical
outputs. Requires: pillow, numpy (development only).
"""

import math
import random
import wave
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
A = ROOT / "assets"
TILE = 16
GRID = 16
ATLAS_PX = TILE * GRID
SR = 22050

# ----------------------------------------------------------------------------
# tile painting helpers
# ----------------------------------------------------------------------------

def noise_fill(base, speckle, seed, alpha=255):
    rng = np.random.default_rng(seed)
    rgb = np.array(base, dtype=np.int16)
    delta = rng.integers(-speckle, speckle + 1, (TILE, TILE, 3))
    out = np.clip(rgb[None, None, :] + delta, 0, 255).astype(np.uint8)
    a = np.full((TILE, TILE), alpha, np.uint8)
    return np.dstack([out, a])


def to_img(arr):
    return Image.fromarray(arr, "RGBA")


def from_img(img):
    return np.array(img)


def paint_at(atlas_img, slot, arr):
    col, row = slot % GRID, slot // GRID
    tile_img = to_img(arr)
    atlas_img.paste(tile_img, (col * TILE, row * TILE))


def vgrad(arr, top, bottom):
    """vertical shade multiplier per row (tuples 0..255 rgb)"""
    out = arr.astype(np.float32)
    h = arr.shape[0]
    for y in range(h):
        t = y / max(1, h - 1)
        f = [top[i] + (bottom[i] - top[i]) * t for i in range(3)]
        out[..., :3] = np.where(out[..., 3:4] > 0,
                                np.clip(out[..., :3] * (np.array(f) / 255.0 * 1.6), 0, 255),
                                out[..., :3])
    return out.astype(np.uint8)


def draw_on(arr):
    img = to_img(arr)
    return img, ImageDraw.Draw(img)


# ----------------------------------------------------------------------------
# block tile painters (order matches include/wl/tiles.h exactly)
# ----------------------------------------------------------------------------

def t_stone():
    a = noise_fill((125, 125, 128), 13, 101)
    rng = random.Random(102)
    for _ in range(6):
        x, y = rng.randrange(16), rng.randrange(16)
        a[y, x, :3] = (98, 98, 102)
    return a


def t_dirt():
    return noise_fill((134, 96, 67), 16, 103)


def t_grass_top():
    return noise_fill((106, 170, 64), 18, 104)


def t_grass_side():
    a = noise_fill((134, 96, 67), 16, 105)
    rng = random.Random(106)
    for x in range(16):
        depth = 3 if rng.random() < 0.6 else 4
        for y in range(depth):
            g = 106 + rng.randint(-14, 14)
            a[y, x, :3] = (g - 40, g + 40, g - 90)
    return a


def t_cobble():
    a = noise_fill((112, 112, 115), 8, 107)
    rng = random.Random(108)
    for cy in (0, 8):
        for cx in (0, 8):
            ox, oy = cx + rng.randint(-1, 1), cy + rng.randint(-1, 1)
            w, h = 6 + rng.randint(0, 2), 6 + rng.randint(0, 2)
            for y in range(oy, min(16, oy + h)):
                for x in range(ox, min(16, ox + w)):
                    edge = (y == oy or y == oy + h - 1 or x == ox or x == ox + w - 1)
                    v = 128 + rng.randint(-6, 6) if not edge else 84
                    a[y % 16, x % 16, :3] = (v, v, v + 2)
    return a


def t_planks():
    a = noise_fill((162, 130, 78), 7, 109)
    for y in (3, 7, 11, 15):
        a[y, :, :3] = (112, 88, 50)
    rng = random.Random(110)
    for band, y0 in enumerate((0, 4, 8, 12)):
        jx = rng.randint(2, 13)
        for y in range(y0, y0 + 3):
            a[y, jx, :3] = (128, 100, 58)
    for x in range(16):
        if x % 3 == 0:
            for y in range(16):
                if a[y, x, 3] and rng.random() < 0.4:
                    a[y, x, :3] = np.clip(a[y, x, :3].astype(int) - 14, 0, 255)
    return a


def t_sand():
    return noise_fill((219, 207, 163), 10, 111)


def t_sandstone():
    a = noise_fill((214, 200, 155), 6, 112)
    for y in (4, 10):
        a[y, :, :3] = (188, 172, 128)
    for y in (5, 11):
        a[y, :, :3] = (200, 184, 140)
    return a


def t_water():
    a = noise_fill((52, 110, 197), 5, 113)
    for y in range(16):
        if y % 5 == 1:
            for x in range(16):
                a[y, x, :3] = (72, 130, 214)
    return a


def t_glass():
    a = np.zeros((16, 16, 4), np.uint8)
    a[1:15, 1:15] = (0, 0, 0, 0)
    a[0, :, :] = (222, 236, 240, 255)
    a[15, :, :] = (222, 236, 240, 255)
    a[:, 0, :] = (222, 236, 240, 255)
    a[:, 15, :] = (222, 236, 240, 255)
    for i in range(4):
        a[3 + i, 10 - i] = (235, 245, 250, 170)
        a[6 + i, 13 - i] = (235, 245, 250, 140)
    return a


def t_log_side():
    rng = random.Random(114)
    a = np.zeros((16, 16, 4), np.uint8)
    for x in range(16):
        base = 103 + rng.randint(-9, 9)
        for y in range(16):
            v = base + rng.randint(-5, 5)
            a[y, x, :3] = (v, int(v * 0.8), int(v * 0.48))
        if rng.random() < 0.3:
            y0 = rng.randrange(16)
            for y in range(y0, min(16, y0 + rng.randint(3, 7))):
                a[y, x, :3] = (72, 56, 34)
    a[..., 3] = 255
    return a


def t_log_top():
    a = np.zeros((16, 16, 4), np.uint8)
    for y in range(16):
        for x in range(16):
            d = math.hypot(x - 7.5, y - 7.5)
            ring = int(d) % 2
            if d > 7.6:
                c = (103, 82, 49)
            elif ring == 0:
                c = (149, 121, 73)
            else:
                c = (128, 103, 61)
            a[y, x, :3] = c
    a[..., 3] = 255
    return a


def t_leaves(base, seed, hole_chance=0.22):
    rng = np.random.default_rng(seed)
    a = np.zeros((16, 16, 4), np.uint8)
    for y in range(16):
        for x in range(16):
            if rng.random() < hole_chance:
                continue
            v = rng.integers(-18, 19)
            a[y, x, :3] = np.clip(np.array(base) + (v, int(v * 1.4), v), 0, 255)
            a[y, x, 3] = 255
    return a


def t_ore(blob_color, seed):
    a = t_stone()
    rng = random.Random(seed)
    for _ in range(4):
        cx, cy = rng.randint(2, 12), rng.randint(2, 12)
        for _ in range(rng.randint(3, 6)):
            dx, dy = rng.randint(-1, 1), rng.randint(-1, 1)
            x, y = min(15, max(0, cx + dx)), min(15, max(0, cy + dy))
            shade = rng.randint(-12, 12)
            a[y, x, :3] = np.clip(np.array(blob_color) + shade, 0, 255)
    return a


def t_bedrock():
    rng = random.Random(115)
    pal = [(38, 38, 42), (66, 66, 70), (26, 26, 29), (90, 90, 96)]
    a = np.zeros((16, 16, 4), np.uint8)
    for y in range(16):
        for x in range(16):
            a[y, x, :3] = pal[rng.randrange(4)]
    a[..., 3] = 255
    return a


def t_snow():
    return noise_fill((245, 248, 250), 4, 116)


def t_grass_snow_side():
    a = t_dirt()
    rng = random.Random(117)
    for x in range(16):
        depth = 4 if rng.random() < 0.5 else 5
        for y in range(depth):
            a[y, x, :3] = (243 + rng.randint(-6, 6), 246 + rng.randint(-5, 5), 250)
    return a


def t_ice():
    a = noise_fill((160, 200, 240), 5, 118, alpha=215)
    for i in range(6):
        x = 2 + i * 2
        for y in range(16):
            if (x + y) % 7 == 0:
                a[y, min(15, x), :3] = (200, 228, 250)
    return a


def t_cactus_side():
    a = noise_fill((58, 124, 48), 6, 119)
    for x in range(0, 16, 4):
        a[:, x, :3] = (44, 100, 38)
    rng = random.Random(120)
    for y in range(1, 16, 3):
        a[y, 1, :3] = (230, 235, 220)
        a[y, 14, :3] = (230, 235, 220)
    return a


def t_cactus_top():
    a = noise_fill((70, 138, 56), 6, 121)
    for i in range(16):
        a[i, 0, :3] = (44, 100, 38)
        a[i, 15, :3] = (44, 100, 38)
        a[0, i, :3] = (44, 100, 38)
        a[15, i, :3] = (44, 100, 38)
    a[6:10, 6:10, :3] = (88, 152, 70)
    return a


def t_crafting_top():
    a = t_planks()
    for i in range(16):
        a[0, i, :3] = (92, 70, 40)
        a[15, i, :3] = (92, 70, 40)
        a[i, 0, :3] = (92, 70, 40)
        a[i, 15, :3] = (92, 70, 40)
        a[7, i, :3] = (120, 94, 54)
        a[8, i, :3] = (120, 94, 54)
        a[i, 7, :3] = (120, 94, 54)
        a[i, 8, :3] = (120, 94, 54)
    return a


def t_crafting_side():
    a = t_planks()
    a[2:7, 2:8] = (92, 70, 40, 255)
    a[3:6, 3:7] = (60, 46, 28, 255)
    return a


def t_crafting_front():
    a = t_planks()
    a[2:8, 2:7] = (88, 66, 38, 255)
    # crude saw silhouette
    for i in range(6):
        a[3 + i, 8 + i, :3] = (180, 184, 190)
        a[3 + i, 9 + i, :3] = (140, 144, 150)
    return a


def t_torch():
    a = np.zeros((16, 16, 4), np.uint8)
    for y in range(6, 16):
        a[y, 7, :3] = (109, 85, 50)
        a[y, 8, :3] = (96, 74, 42)
    a[5, 7, :3] = (255, 216, 81)
    a[5, 8, :3] = (255, 216, 81)
    a[4, 7, :3] = (255, 176, 59)
    a[4, 8, :3] = (255, 216, 81)
    a[3, 7, :3] = (255, 140, 40)
    a[..., 3] = np.where(a[..., :3].sum(axis=2) > 0, 255, 0)
    return a


def t_flower(petal, center, seed):
    a = np.zeros((16, 16, 4), np.uint8)
    for y in range(8, 16):
        a[y, 7, :3] = (62, 124, 44)
        a[y, 8, :3] = (52, 108, 38)
    rng = random.Random(seed)
    for y in range(6, 10):
        for x in range(5, 11):
            d = math.hypot(x - 7.5, y - 7.5)
            if d < 2.8:
                a[y, x, :3] = petal if rng.random() < 0.85 else center
                a[y, x, 3] = 255
    a[7, 7, :3] = center
    a[7, 8, :3] = center
    return a


def t_tallgrass():
    a = np.zeros((16, 16, 4), np.uint8)
    rng = random.Random(122)
    for blade in range(7):
        x = 2 + rng.randrange(12)
        h = rng.randint(6, 12)
        for i in range(h):
            y = 15 - i
            xx = min(15, max(0, x + (1 if i > h // 2 and rng.random() < 0.2 else 0)))
            g = 96 + rng.randint(-20, 40)
            a[y, xx, :3] = (int(g * 0.55), g, int(g * 0.4))
            a[y, xx, 3] = 255
    return a


# ---- item icons ----

def t_stick_icon():
    a = np.zeros((16, 16, 4), np.uint8)
    for i in range(8):
        x, y = 4 + i, 11 - i
        a[y, x, :3] = (146, 111, 63)
        a[y, x, 3] = 255
        if x + 1 < 16:
            a[y, x + 1, :3] = (109, 85, 50)
            a[y, x + 1, 3] = 255
    return a


def t_lump(color, seed):
    a = np.zeros((16, 16, 4), np.uint8)
    rng = random.Random(seed)
    for y in range(4, 13):
        for x in range(4, 13):
            d = math.hypot(x - 8, y - 8)
            if d < 4.2 + rng.random() * 0.5:
                c = np.array(color) + rng.randint(-14, 14)
                a[y, x, :3] = np.clip(c, 0, 255)
                a[y, x, 3] = 255
    a[6, 6, :3] = np.clip(np.array(color) + 50, 0, 255)
    return a


def t_diamond_icon():
    a = np.zeros((16, 16, 4), np.uint8)
    for y in range(16):
        for x in range(16):
            d = math.hypot((x - 8) * 1.2, y - 8)
            if d < 5:
                c = (60, 190, 190) if y > 8 else (110, 226, 220)
                a[y, x, :3] = c
                a[y, x, 3] = 255
    a[6, 6, :3] = (220, 250, 250)
    return a


def t_apple():
    a = np.zeros((16, 16, 4), np.uint8)
    for y in range(5, 14):
        for x in range(4, 12):
            d = math.hypot(x - 7.5, (y - 9) * 1.15)
            if d < 4.4:
                a[y, x, :3] = (198, 40, 40)
                a[y, x, 3] = 255
    a[6, 6, :3] = (240, 120, 110)
    a[3:5, 8, :3] = (101, 67, 33)
    a[3:5, 8, 3] = 255
    a[4, 10, :3] = (80, 160, 60)
    a[5, 10, :3] = (80, 160, 60)
    a[4, 10, 3] = 255
    a[5, 10, 3] = 255
    return a


TIER_COLORS = {
    "wood": (151, 111, 60),
    "stone": (125, 125, 128),
    "iron": (206, 206, 212),
    "diamond": (80, 220, 215),
}
HANDLE = (109, 85, 50)


def t_tool(kind, tier):
    head = TIER_COLORS[tier]
    a = np.zeros((16, 16, 4), np.uint8)

    def px(x, y, c):
        if 0 <= x < 16 and 0 <= y < 16:
            a[y, x, :3] = c
            a[y, x, 3] = 255

    def handle(x0, y0, x1, y1):
        n = max(abs(x1 - x0), abs(y1 - y0))
        for i in range(n + 1):
            t = i / max(1, n)
            x = int(round(x0 + (x1 - x0) * t))
            y = int(round(y0 + (y1 - y0) * t))
            px(x, y, HANDLE)
            px(x + 1, y, (88, 68, 40))

    if kind == "pick":
        handle(4, 11, 11, 4)
        for x in range(3, 13):
            px(x, 2, head)
        px(2, 3, head); px(3, 3, head); px(12, 3, head); px(13, 3, head)
        px(2, 4, head); px(13, 4, head)
        px(2, 5, head); px(13, 5, head)
    elif kind == "axe":
        handle(4, 12, 10, 5)
        for y in range(1, 7):
            for x in range(6, 12):
                if (x - 8) + (y - 3) < 5 and (x - 8) + (y - 3) > -3 and y > 1:
                    px(x, y, head)
        px(6, 1, head); px(7, 1, head)
    elif kind == "shovel":
        handle(4, 12, 10, 5)
        for y in range(0, 6):
            for x in range(8, 14):
                if abs(x - 11) + abs(y - 3) < 5:
                    px(x, y, head)
    elif kind == "sword":
        for i in range(8):
            x, y = 4 + i, 12 - i
            px(x, y, head)
            px(x + 1, y - 1, head)
        px(4, 13, (60, 50, 40)); px(3, 14, HANDLE)
        px(5, 13, (180, 160, 90)); px(3, 11, (180, 160, 90)); px(6, 13, (180, 160, 90)); px(4, 11, (180, 160, 90))
    return a



# ---- furnace + food/material/crack/arrow painters (order matches tiles.h) ----

def t_furnace_side():
    a = t_stone()
    rng = random.Random(301)
    for y in range(16):
        for x in range(16):
            if (y == 0 or y == 15 or x == 0 or x == 15) and rng.random() < 0.5:
                v = int(a[y, x, 0]) - 14
                a[y, x, :3] = (max(0, v),) * 3
    return a


def t_furnace_front(lit=False):
    a = t_furnace_side()
    # firebox opening
    a[9:14, 4:12] = (28, 26, 24, 255)
    if lit:
        rng = random.Random(302)
        for y in range(10, 14):
            for x in range(5, 11):
                if rng.random() < 0.55 - (y - 10) * 0.1:
                    a[y, x, :3] = (255, 170 + rng.randint(-30, 40), 40)
                else:
                    a[y, x, :3] = (120, 40, 20)
    else:
        a[9:14, 4:12] = (22, 20, 18, 255)
    # rim
    for x in range(4, 12):
        a[8, x, :3] = (70, 70, 72)
    return a


def t_furnace_top():
    a = t_stone()
    for i in range(16):
        a[i, 0, :3] = (58, 58, 60)
        a[i, 15, :3] = (58, 58, 60)
        a[0, i, :3] = (58, 58, 60)
        a[15, i, :3] = (58, 58, 60)
    a[5:11, 5:11] = (96, 96, 98, 255)
    return a


def _meat(raw, bone_in=False):
    a = np.zeros((16, 16, 4), np.uint8)
    body = raw
    rng = random.Random(303)
    for y in range(4, 13):
        for x in range(3, 13):
            dx = (x - 8) / 5.0
            dy = (y - 8.2) / 4.2
            if dx * dx + dy * dy < 1.0:
                c = body
                if rng.random() < 0.25:
                    n = rng.randint(-18, 18)
                    c = tuple(max(0, min(255, v + n)) for v in body)
                a[y, x, :3] = c
                a[y, x, 3] = 255
    if bone_in:  # drumstick bone
        for i in range(5):
            a[12 - i, 11 + i // 2, :3] = (235, 228, 210)
            a[12 - i, 11 + i // 2, 3] = 255
        a[13, 13, :3] = (235, 228, 210); a[13, 13, 3] = 255
        a[13, 11, :3] = (235, 228, 210); a[13, 11, 3] = 255
    return a


def t_leather_icon():
    a = np.zeros((16, 16, 4), np.uint8)
    rng = random.Random(304)
    for y in range(3, 13):
        for x in range(3, 13):
            if (x in (3, 12) and y in (3, 12)):
                continue
            c = (146, 102, 62)
            if rng.random() < 0.3:
                n = rng.randint(-14, 10)
                c = tuple(max(0, v + n) for v in c)
            a[y, x, :3] = c
            a[y, x, 3] = 255
    return a


def t_feather_icon():
    a = np.zeros((16, 16, 4), np.uint8)
    rng = random.Random(305)
    for i in range(10):
        y = 13 - i
        x = 4 + i // 2
        w = 1 + (i if i < 4 else 4) // 2
        for k in range(w + 1):
            if 0 <= x + k < 16 and 0 <= y < 16:
                c = (238, 240, 244) if k < w else (200, 202, 208)
                if rng.random() < 0.2:
                    c = tuple(v - 14 for v in c)
                a[y, x + k, :3] = c
                a[y, x + k, 3] = 255
    a[13, 3, :3] = (160, 150, 140); a[13, 3, 3] = 255
    return a


def t_wool_icon():
    a = np.zeros((16, 16, 4), np.uint8)
    rng = random.Random(306)
    for y in range(2, 14):
        for x in range(2, 14):
            if (x - 8) ** 2 + (y - 8) ** 2 < 36:
                n = rng.randint(-10, 12)
                a[y, x, :3] = (232 + n // 2, 230 + n // 2, 226 + n // 2)
                a[y, x, 3] = 255
    return a


def t_ingot(color=(216, 200, 190), seed=307):
    a = np.zeros((16, 16, 4), np.uint8)
    rng = random.Random(seed)
    for y in range(6, 12):
        x0 = 2 + (11 - y) // 2
        x1 = 13 - (11 - y) // 2
        for x in range(x0, x1):
            shade = 1.0 - 0.04 * (11 - y)
            c = tuple(min(255, int(v * shade + rng.randint(-6, 6))) for v in color)
            a[y, x, :3] = c
            a[y, x, 3] = 255
    return a


def t_rotten_flesh_icon():
    a = _meat((120, 92, 60))
    rng = random.Random(310)
    for _ in range(10):
        y = rng.randrange(5, 12)
        x = rng.randrange(4, 12)
        a[y, x, :3] = (86, 110, 58)  # mouldy green spots
    return a


def t_string_icon():
    a = np.zeros((16, 16, 4), np.uint8)
    rng = random.Random(311)
    y = 2.0
    for x in range(3, 13):
        y += rng.uniform(-0.8, 0.8)
        iy = int(round(y))
        for k in (-1, 0, 1):
            if 0 <= iy + k < 16:
                a[iy + k, x, :3] = (226, 222, 210)
                a[iy + k, x, 3] = 255
    return a


def t_bone_icon():
    a = np.zeros((16, 16, 4), np.uint8)
    for i in range(9):
        x = 3 + i
        y = 12 - i
        a[y, x, :3] = (232, 226, 210); a[y, x, 3] = 255
        a[y, x + 1, :3] = (216, 210, 194); a[y, x + 1, 3] = 255
    for dx, dy in ((-1, 1), (0, 0), (0, 2), (1, -1), (1, 1)):
        a[4 + dy, 11 + dx, :3] = (240, 236, 224)
        a[4 + dy, 11 + dx, 3] = 255
    for dx, dy in ((-1, -1), (-1, 1), (0, 0), (1, 0), (0, 1)):
        a[10 + dy, 4 + dx, :3] = (240, 236, 224)
        a[10 + dy, 4 + dx, 3] = 255
    return a


def t_arrow_icon():
    a = np.zeros((16, 16, 4), np.uint8)
    for i in range(9):
        x = 3 + i
        y = 12 - i
        a[y, x, :3] = (109, 85, 50); a[y, x, 3] = 255
        a[y, x + 1, :3] = (96, 74, 42); a[y, x + 1, 3] = 255
    # head
    for dx, dy in ((0, 0), (-1, -1), (1, 1), (0, -1), (0, 1)):
        a[12 - 8 + dy, 11 + dx, :3] = (208, 212, 218)
        a[12 - 8 + dy, 11 + dx, 3] = 255
    # fletching
    for i in range(3):
        a[12 - i, 3 + i, :3] = (238, 240, 244); a[12 - i, 3 + i, 3] = 255
        a[13 - i, 4 + i, :3] = (210, 212, 218); a[13 - i, 4 + i, 3] = 255
    return a


def t_crack(stage):
    """Progressive crack pattern: stage 0..9, dark alpha cracks radiating out."""
    a = np.zeros((16, 16, 4), np.uint8)
    alpha = 190 + stage * 6
    dark = np.array([12, 10, 9, alpha], np.uint8)
    # one main crack for stage 0, more branches as damage grows
    n_lines = 2 + stage
    for idx in range(n_lines):
        rng = random.Random(500 + idx)
        ang = (idx * 2.399963) % (2 * 3.14159265)
        x, y = 7.5, 7.5
        dx, dy = math.cos(ang), math.sin(ang)
        length = 3.0 + stage * 1.1
        for step in range(int(length * 2)):
            t = step / max(1.0, length * 2)
            # wobble for a natural look
            wob = (rng.random() - 0.5) * 0.6
            x = 7.5 + (dx + wob * dy) * t * (2.0 + stage * 0.85)
            y = 7.5 + (dy + wob * dx) * t * (2.0 + stage * 0.85)
            ix, iy = int(round(x)), int(round(y))
            for ox in (0, 1):
                for oy in (0, 1):
                    jx, jy = ix + ox, iy + oy
                    if 0 <= jx < 16 and 0 <= jy < 16 and rng.random() < 0.8:
                        a[jy, jx] = dark
    return a


TILE_PAINTERS = [
    ("stone", t_stone),                       # 0
    ("dirt", t_dirt),                         # 1
    ("grass_top", t_grass_top),               # 2
    ("grass_side", t_grass_side),             # 3
    ("cobble", t_cobble),                     # 4
    ("planks", t_planks),                     # 5
    ("sand", t_sand),                         # 6
    ("sandstone", t_sandstone),               # 7
    ("water", t_water),                       # 8
    ("glass", t_glass),                       # 9
    ("log_side", t_log_side),                 # 10
    ("log_top", t_log_top),                   # 11
    ("leaves", lambda: t_leaves((58, 122, 40), 123)),        # 12
    ("birch_log_side", None),                 # 13 (set below)
    ("birch_log_top", None),                  # 14
    ("birch_leaves", lambda: t_leaves((116, 156, 74), 124)), # 15
    ("gravel", None),                         # 16
    ("coal_ore", None),                       # 17
    ("iron_ore", None),                       # 18
    ("gold_ore", None),                       # 19
    ("diamond_ore", None),                    # 20
    ("bedrock", t_bedrock),                   # 21
    ("snow", t_snow),                         # 22
    ("grass_snow_side", t_grass_snow_side),   # 23
    ("ice", t_ice),                           # 24
    ("cactus_side", t_cactus_side),           # 25
    ("cactus_top", t_cactus_top),             # 26
    ("crafting_top", t_crafting_top),         # 27
    ("crafting_side", t_crafting_side),       # 28
    ("crafting_front", t_crafting_front),     # 29
    ("torch", t_torch),                       # 30
    ("flower_red", lambda: t_flower((205, 48, 48), (250, 200, 90), 125)),   # 31
    ("flower_yellow", lambda: t_flower((240, 200, 60), (170, 120, 40), 126)),  # 32
    ("tallgrass", t_tallgrass),               # 33
    ("icon_stick", t_stick_icon),             # 34
    ("icon_coal", lambda: t_lump((40, 40, 44), 127)),        # 35
    ("icon_raw_iron", lambda: t_lump((206, 164, 140), 128)), # 36
    ("icon_raw_gold", lambda: t_lump((250, 214, 90), 129)),  # 37
    ("icon_diamond", t_diamond_icon),         # 38
    ("icon_apple", t_apple),                  # 39
]

# birch / gravel / ore tiles referenced by index above
def _birch_side():
    a = noise_fill((196, 187, 168), 5, 134)
    rng = random.Random(135)
    for _ in range(5):
        y = rng.randrange(1, 15)
        x = rng.randrange(0, 9)
        w = rng.randint(3, 6)
        for i in range(w):
            a[y, min(15, x + i), :3] = (86, 80, 70)
    return a


def _birch_top():
    a = t_log_top()
    a[..., :3] = np.clip(a[..., :3].astype(int) + [40, 44, 34], 0, 255).astype(np.uint8)
    return a


def _gravel():
    rng = random.Random(136)
    pal = [(120, 115, 110), (140, 135, 128), (100, 95, 90), (152, 147, 140), (88, 84, 80)]
    a = np.zeros((16, 16, 4), np.uint8)
    for y in range(16):
        for x in range(16):
            a[y, x, :3] = pal[rng.randrange(len(pal))]
    a[..., 3] = 255
    return a


TILE_PAINTERS[13] = ("birch_log_side", lambda: _birch_side())
TILE_PAINTERS[14] = ("birch_log_top", lambda: _birch_top())
TILE_PAINTERS[16] = ("gravel", lambda: _gravel())
TILE_PAINTERS[17] = ("coal_ore", lambda: t_ore((35, 35, 38), 130))
TILE_PAINTERS[18] = ("iron_ore", lambda: t_ore((216, 175, 147), 131))
TILE_PAINTERS[19] = ("gold_ore", lambda: t_ore((252, 222, 112), 132))
TILE_PAINTERS[20] = ("diamond_ore", lambda: t_ore((92, 219, 213), 133))


# tool icons appended after the base 40 tiles (order must match tiles.h)
for kind in ("pick", "axe", "shovel", "sword"):
    for tier in ("wood", "stone", "iron", "diamond"):
        TILE_PAINTERS.append((f"icon_{kind}_{tier}", (lambda k=kind, t=tier: t_tool(k, t))))

TILE_PAINTERS += [
    ("furnace_side", t_furnace_side),                       # 64
    ("furnace_front", lambda: t_furnace_front(False)),      # 65
    ("furnace_front_lit", lambda: t_furnace_front(True)),   # 66
    ("furnace_top", t_furnace_top),                         # 67
    ("icon_leather", t_leather_icon),                       # 68
    ("icon_feather", t_feather_icon),                       # 69
    ("icon_wool", t_wool_icon),                             # 70
    ("icon_iron_ingot", lambda: t_ingot((216, 200, 190))),  # 71
    ("icon_beef_raw", lambda: _meat((196, 70, 62))),        # 72
    ("icon_beef_cooked", lambda: _meat((146, 84, 44))),     # 73
    ("icon_pork_raw", lambda: _meat((235, 154, 148))),      # 74
    ("icon_pork_cooked", lambda: _meat((188, 124, 76))),    # 75
    ("icon_chicken_raw", lambda: _meat((232, 184, 168), bone_in=True)),   # 76
    ("icon_chicken_cooked", lambda: _meat((196, 132, 74), bone_in=True)), # 77
    ("icon_mutton_raw", lambda: _meat((214, 96, 92))),      # 78
    ("icon_mutton_cooked", lambda: _meat((158, 94, 54))),   # 79
    ("icon_arrow", t_arrow_icon),                           # 72
    ("icon_rotten_flesh", t_rotten_flesh_icon),             # 73
    ("icon_string", t_string_icon),                         # 74
    ("icon_bone", t_bone_icon),                             # 75
] + [("crack_%d" % s, (lambda s=s: t_crack(s))) for s in range(10)]  # 76..85



def build_atlas():
    # cross-check against include/wl/tiles.h
    tiles_h = (ROOT / "include" / "wl" / "tiles.h").read_text()
    import re
    names = re.findall(r"^\s+TILE_(\w+)(?:\s*=\s*\d+)?,", tiles_h, re.M)
    names = [n.lower() for n in names if n != "BASE_COUNT"]
    ours = [n for n, _ in TILE_PAINTERS]
    if names != ours:
        raise SystemExit(f"tile layout mismatch!\n tiles.h: {names}\n generator: {ours}")

    atlas = Image.new("RGBA", (ATLAS_PX, ATLAS_PX), (0, 0, 0, 0))
    for slot, (name, painter) in enumerate(TILE_PAINTERS):
        paint_at(atlas, slot, painter())
    # fixed white tile (slot 250) for solid-colour box models; see tiles.h
    white = Image.new("RGBA", (TILE, TILE), (255, 255, 255, 255))
    atlas.paste(white, ((250 % 16) * TILE, (250 // 16) * TILE))
    out = A / "textures" / "blocks.png"
    atlas.save(out)
    print(f"wrote {out} ({len(TILE_PAINTERS)} tiles)")


# ----------------------------------------------------------------------------
# GUI textures
# ----------------------------------------------------------------------------

def gui_button():
    img = Image.new("RGBA", (48, 16), (62, 66, 72, 255))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 47, 15], outline=(30, 32, 36, 255))
    d.line([1, 1, 46, 1], fill=(110, 116, 124, 255))
    d.line([1, 1, 1, 14], fill=(110, 116, 124, 255))
    d.line([1, 14, 46, 14], fill=(44, 46, 50, 255))
    d.line([46, 1, 46, 14], fill=(44, 46, 50, 255))
    rng = random.Random(201)
    px = img.load()
    for y in range(2, 14):
        for x in range(2, 46):
            if rng.random() < 0.3:
                n = rng.randint(-4, 4)
                r, g, b, a = px[x, y]
                px[x, y] = (max(0, min(255, r + n)), max(0, min(255, g + n)),
                            max(0, min(255, b + n)), a)
    img.save(A / "gui" / "button.png")


def gui_panel():
    img = Image.new("RGBA", (96, 96), (28, 30, 34, 255))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 95, 95], outline=(12, 13, 15, 255))
    d.rectangle([1, 1, 94, 94], outline=(58, 62, 68, 255))
    rng = random.Random(202)
    px = img.load()
    for y in range(3, 93):
        for x in range(3, 93):
            if rng.random() < 0.25:
                n = rng.randint(-3, 3)
                r, g, b, a = px[x, y]
                px[x, y] = (max(0, r + n), max(0, g + n), max(0, b + n), a)
    img.save(A / "gui" / "panel.png")


def gui_slot(sel=False):
    if sel:
        # selection highlight: a BORDER ONLY. It is drawn on top of the
        # slot and its item icon, so the center must stay transparent —
        # an opaque plate here hides the selected item ("invisible held
        # item" bug).
        img = Image.new("RGBA", (20, 20), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.rectangle([0, 0, 19, 19], outline=(240, 230, 170, 255))
        d.rectangle([1, 1, 18, 18], outline=(240, 230, 170, 255))
    else:
        img = Image.new("RGBA", (20, 20), (20, 22, 26, 255))
        d = ImageDraw.Draw(img)
        d.line([1, 1, 18, 1], fill=(12, 13, 15, 255))
        d.line([1, 1, 1, 18], fill=(12, 13, 15, 255))
        d.line([1, 18, 18, 18], fill=(74, 78, 84, 255))
        d.line([18, 1, 18, 18], fill=(74, 78, 84, 255))
    img.save(A / "gui" / ("slot_selected.png" if sel else "slot.png"))


def gui_logo():
    font_path = A / "fonts" / "PressStart2P-Regular.ttf"
    font = ImageFont.truetype(str(font_path), 26)
    text = "WILDLANDS"
    tmp = Image.new("RGBA", (16, 16))
    d = ImageDraw.Draw(tmp)
    bbox = d.textbbox((0, 0), text, font=font)
    w = bbox[2] - bbox[0] + 16
    h = bbox[3] - bbox[1] + 20
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.text((9, 11), text, font=font, fill=(16, 22, 18, 255))          # shadow
    d.text((8, 8), text, font=font, fill=(235, 240, 225, 255))        # body
    d.text((8, 6), text, font=font, fill=(255, 255, 255, 255))        # top shine
    # green underline accent
    d.line([8, h - 8, 8 + (bbox[2] - bbox[0]), h - 8], fill=(106, 170, 64, 255), width=3)
    img.save(A / "gui" / "logo.png")


def gui_heart(full=True):
    pat = [
        ".XX.XX.",
        "XXXXXXX",
        "XXXXXXX",
        ".XXXXX.",
        "..XXX..",
        "...X...",
    ]
    img = Image.new("RGBA", (9, 9), (0, 0, 0, 0))
    px = img.load()
    for y, row in enumerate(pat):
        for x, c in enumerate(row):
            if c == "X":
                if full:
                    v = (208, 42, 42, 255) if not (1 <= x <= 2 and y == 1) else (245, 120, 110, 255)
                else:
                    v = (52, 54, 58, 220)
                px[x + 1, y + 1] = v
    img.save(A / "gui" / ("heart.png" if full else "heart_empty.png"))


def gui_hunger(full=True):
    img = Image.new("RGBA", (9, 9), (0, 0, 0, 0))
    px = img.load()
    meat = (150, 92, 48, 255) if full else (52, 54, 58, 220)
    bone = (235, 232, 220, 255) if full else (60, 62, 66, 220)
    for y in range(1, 6):
        for x in range(3, 9):
            if math.hypot(x - 5.5, y - 3) < 2.6:
                px[x, y] = meat
    px[2, 6] = bone; px[1, 7] = bone; px[1, 8] = bone; px[2, 7] = bone
    img.save(A / "gui" / ("hunger.png" if full else "hunger_empty.png"))


def gui_crosshair():
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    px = img.load()
    for i in range(16):
        if abs(i - 7.5) > 2.5:
            px[i, 7] = (245, 245, 245, 210)
            px[i, 8] = (245, 245, 245, 210)
            px[7, i] = (245, 245, 245, 210)
            px[8, i] = (245, 245, 245, 210)
    img.save(A / "gui" / "crosshair.png")


def env_sun():
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    px = img.load()
    for y in range(32):
        for x in range(32):
            d = max(abs(x - 15.5), abs(y - 15.5))
            if d < 9:
                px[x, y] = (255, 238, 150, 255)
            elif d < 11:
                px[x, y] = (255, 244, 180, 200)
            elif d < 14:
                px[x, y] = (255, 240, 160, 70)
    img.save(A / "environment" / "sun.png")


def env_moon():
    img = Image.new("RGBA", (28, 28), (0, 0, 0, 0))
    px = img.load()
    for y in range(28):
        for x in range(28):
            d = math.hypot(x - 13.5, y - 13.5)
            if d < 10:
                px[x, y] = (222, 228, 236, 255)
    for cx, cy, r in ((10, 9, 2), (17, 15, 1.6), (12, 18, 1.3)):
        for y in range(28):
            for x in range(28):
                if math.hypot(x - cx, y - cy) < r:
                    px[x, y] = (190, 198, 210, 255)
    img.save(A / "environment" / "moon.png")


def env_clouds():
    size = 256
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    rng = random.Random(301)
    for _ in range(26):
        cx, cy = rng.randrange(size), rng.randrange(size)
        rw, rh = rng.randint(14, 40), rng.randint(8, 18)
        for ox in (-size, 0, size):
            for oy in (-size, 0, size):
                x0, y0 = cx + ox, cy + oy
                if -rw < x0 < size + rw and -rh < y0 < size + rh:
                    d.ellipse([x0, y0, x0 + rw, y0 + rh], fill=(255, 255, 255, 235))
    img.save(A / "environment" / "clouds.png")


# ----------------------------------------------------------------------------
# audio
# ----------------------------------------------------------------------------

def write_wav(path, samples, gain=0.9):
    x = np.clip(samples * gain, -1, 1)
    pcm = (x * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print(f"wrote {path} ({len(x)/SR:.2f}s)")


def env_exp(n, rate):
    return np.exp(-np.arange(n) * rate / SR)


def lowpass(x, a=0.12):
    y = np.empty_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc += a * (v - acc)
        y[i] = acc
    return y


def highpass_band(x, lo, hi):
    """Crude band emphasis: difference of two one-pole lowpasses."""
    a = lowpass(x, hi)
    b = lowpass(x, lo)
    return a - b


def gen_entity_atlas():
    """Mob/animal part skins. 16x16 tiles; each part's four faces map to
    quadrants: front=TL, side=TR, top=BL, bottom=BR."""
    def skin(base, speckle, seed, features=None):
        a = noise_fill(base, speckle, seed)
        if features:
            features(a)
        return a

    def eyes(a, color, y=5, x0=3, x1=11):
        for x in (x0, x1):
            for dy in range(2):
                for dx in range(2):
                    a[y + dy, x + dx, :3] = color

    E = A / "textures" / "entities.png"
    E.parent.mkdir(parents=True, exist_ok=True)
    atlas = Image.new("RGBA", (256, 256), (0, 0, 0, 0))

    slots = {}

    def put(name, painter):
        idx = len(slots)
        slots[name] = idx
        paint_at(atlas, idx, painter())

    put("zombie_head", lambda: skin((96, 138, 78), 8, 601,
                                    lambda a: eyes(a, (20, 28, 20))))
    put("zombie_body", lambda: skin((58, 88, 120), 7, 602))
    put("zombie_limb", lambda: skin((84, 120, 70), 8, 603))
    put("skel_head", lambda: skin((206, 202, 190), 6, 604,
                                  lambda a: eyes(a, (30, 30, 30))))
    put("skel_body", lambda: skin((188, 184, 172), 6, 605))
    put("skel_limb", lambda: skin((198, 194, 182), 5, 606))
    put("spider_body", lambda: skin((44, 38, 40), 6, 607))
    put("spider_head", lambda: skin((52, 44, 46), 6, 608,
                                    lambda a: eyes(a, (196, 40, 40), y=5, x0=4, x1=10)))
    put("spider_leg", lambda: skin((38, 33, 35), 5, 609))
    put("cow_body", lambda: skin((96, 66, 48), 9, 610))
    put("cow_head", lambda: skin((104, 72, 52), 8, 611,
                                 lambda a: eyes(a, (25, 22, 20))))
    put("cow_leg", lambda: skin((80, 56, 42), 7, 612))
    put("pig_body", lambda: skin((228, 156, 148), 7, 613))
    put("pig_head", lambda: skin((232, 164, 156), 7, 614,
                                 lambda a: eyes(a, (30, 26, 26))))
    put("pig_leg", lambda: skin((214, 142, 134), 6, 615))
    put("chicken_body", lambda: skin((232, 228, 220), 6, 616))
    put("chicken_head", lambda: skin((236, 232, 224), 6, 617,
                                     lambda a: eyes(a, (30, 28, 26))))
    put("chicken_leg", lambda: skin((212, 168, 84), 5, 618))
    put("chicken_wing", lambda: skin((220, 214, 204), 6, 619))
    put("sheep_body", lambda: skin((234, 230, 222), 10, 620))
    put("sheep_head", lambda: skin((198, 178, 152), 7, 621,
                                   lambda a: eyes(a, (30, 28, 26))))
    put("sheep_leg", lambda: skin((210, 196, 172), 6, 622))
    put("arrow", t_arrow_icon)

    atlas.save(E)
    with open(E.with_suffix(".txt"), "w") as f:
        f.write("# entity atlas tile ids (16x16 grid)\n")
        for name, idx in slots.items():
            f.write(f"{name} = {idx}\n")
    print(f"wrote {E} ({len(slots)} part tiles)")


def gen_sounds():
    rng = np.random.default_rng(401)
    out = A / "audio"

    n = int(0.035 * SR)  # click
    write_wav(out / "click.wav", (rng.standard_normal(n) * 0.5 + 0.4) * env_exp(n, 4000))

    n = int(0.1 * SR)  # place: thud
    t = np.arange(n) / SR
    x = np.sin(2 * np.pi * 118 * t) * env_exp(n, 45) * 0.7
    x += lowpass(rng.standard_normal(n), 0.25) * env_exp(n, 70) * 0.5
    write_wav(out / "place.wav", x)

    n = int(0.18 * SR)  # break stone
    x = lowpass(rng.standard_normal(n), 0.22) * env_exp(n, 22) * 1.4
    for i in (int(0.01 * SR), int(0.05 * SR)):
        x[i:i + 120] += rng.standard_normal(120) * 0.5
    write_wav(out / "break_stone.wav", x)

    n = int(0.16 * SR)  # break dirt
    write_wav(out / "break_dirt.wav", lowpass(rng.standard_normal(n), 0.08) * env_exp(n, 20) * 2.2)

    n = int(0.14 * SR)  # break wood: two knocks
    x = np.zeros(n)
    for k in (0, int(0.055 * SR)):
        m = n - k
        t = np.arange(m) / SR
        x[k:] += (np.sin(2 * np.pi * 190 * t) * env_exp(m, 60) +
                  lowpass(rng.standard_normal(m), 0.3) * env_exp(m, 80) * 0.6)
    write_wav(out / "break_wood.wav", x * 0.8)

    n = int(0.08 * SR)  # steps
    write_wav(out / "step_grass.wav", lowpass(rng.standard_normal(n), 0.1) * env_exp(n, 60) * 1.6)
    write_wav(out / "step_stone.wav", lowpass(rng.standard_normal(n), 0.5) * env_exp(n, 90) * 1.2)

    n = int(0.22 * SR)  # hurt: falling square-ish tone
    t = np.arange(n) / SR
    f = 300 - 700 * t
    x = np.sign(np.sin(2 * np.pi * np.cumsum(f) / SR)) * 0.35 * env_exp(n, 14)
    write_wav(out / "hurt.wav", x)

    n = int(0.18 * SR)  # mob hurt: low thud + growl
    t = np.arange(n) / SR
    x = np.sin(2 * np.pi * 96 * t) * env_exp(n, 26) * 0.8
    x += lowpass(rng.standard_normal(n), 0.12) * env_exp(n, 30) * 0.7
    write_wav(out / "mob_hurt.wav", x * 0.8)

    n = int(0.16 * SR)  # bow: whoosh with rising pitch
    t = np.arange(n) / SR
    f = 240 + 900 * t / n
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * env_exp(n, 26) * 0.4
    x += highpass_band(rng.standard_normal(n), 0.35, 0.65) * env_exp(n, 22) * 0.5
    write_wav(out / "bow.wav", x)

    n = int(0.09 * SR)  # throw: short soft whoosh
    t = np.arange(n) / SR
    x = lowpass(rng.standard_normal(n), 0.30) * env_exp(n, 60) * 0.5
    write_wav(out / "throw.wav", x)

    n = int(0.09 * SR)  # step dirt: dull crunch
    write_wav(out / "step_dirt.wav", lowpass(rng.standard_normal(n), 0.06) * env_exp(n, 55) * 2.0)

    n = int(0.07 * SR)  # step wood: hollow knock
    t = np.arange(n) / SR
    x = np.sin(2 * np.pi * 150 * t) * env_exp(n, 80) * 0.6
    x += lowpass(rng.standard_normal(n), 0.2) * env_exp(n, 90) * 0.4
    write_wav(out / "step_wood.wav", x)

    n = int(0.12 * SR)  # step sand: soft shuffle
    write_wav(out / "step_sand.wav", lowpass(rng.standard_normal(n), 0.04) * env_exp(n, 40) * 2.6)

    n = int(0.12 * SR)  # step snow: squeaky crunch
    x = lowpass(rng.standard_normal(n), 0.10) * env_exp(n, 45) * 1.8
    x += np.sin(2 * np.pi * 900 * np.arange(n) / SR) * env_exp(n, 200) * 0.06
    write_wav(out / "step_snow.wav", x)

    n = int(0.10 * SR)  # jump: quick exertion puff
    write_wav(out / "jump.wav", lowpass(rng.standard_normal(n), 0.18) * env_exp(n, 55) * 1.3)

    n = int(0.14 * SR)  # land: heavy thud
    t = np.arange(n) / SR
    x = np.sin(2 * np.pi * 85 * t) * env_exp(n, 30) * 0.9
    x += lowpass(rng.standard_normal(n), 0.10) * env_exp(n, 35) * 0.8
    write_wav(out / "land.wav", x)

    n = int(0.5 * SR)  # death: descending tone
    t = np.arange(n) / SR
    f = 320 - 240 * t / (n / SR)
    x = np.sign(np.sin(2 * np.pi * np.cumsum(f) / SR)) * 0.3 * env_exp(n, 6)
    write_wav(out / "death.wav", x)

    n = int(0.16 * SR)  # sword swing: airy sweep
    t = np.arange(n) / SR
    f = 500 + 1400 * t / (n / SR)
    x = highpass_band(rng.standard_normal(n), 0.30, 0.60) * env_exp(n, 24) * 0.75
    x += np.sin(2 * np.pi * np.cumsum(f) / SR) * env_exp(n, 30) * 0.12
    write_wav(out / "sword_swing.wav", x)

    n = int(0.08 * SR)  # arrow hit: dry thunk
    t = np.arange(n) / SR
    x = np.sin(2 * np.pi * 200 * t) * env_exp(n, 110) * 0.7
    x += lowpass(rng.standard_normal(n), 0.4) * env_exp(n, 120) * 0.5
    write_wav(out / "arrow_hit.wav", x)

    n = int(0.35 * SR)  # furnace ignite: whoosh + crackle
    x = highpass_band(rng.standard_normal(n), 0.10, 0.45) * env_exp(n, 10) * 0.6
    for i in range(0, n, int(0.03 * SR)):
        seg = 60
        x[i:i + seg] += rng.standard_normal(min(seg, n - i)) * 0.25
    write_wav(out / "furnace_ignite.wav", x)

    n = int(0.22 * SR)  # furnace done: bright ding
    t = np.arange(n) / SR
    x = (np.sin(2 * np.pi * 880 * t) + 0.5 * np.sin(2 * np.pi * 1320 * t)) \
        * env_exp(n, 16) * 0.35
    write_wav(out / "furnace_done.wav", x)

    n = int(0.7 * SR)  # zombie groan: low wobbly tone
    t = np.arange(n) / SR
    f = 95 + 18 * np.sin(2 * np.pi * 2.3 * t)
    x = np.sign(np.sin(2 * np.pi * np.cumsum(f) / SR)) * 0.22 * env_exp(n, 5)
    x = lowpass(x, 0.12)
    write_wav(out / "zombie_ambient.wav", x)

    n = int(0.5 * SR)  # skeleton rattle: bone clacks
    x = np.zeros(n)
    for k in range(6):
        i = int((0.02 + 0.075 * k) * SR)
        m = min(240, n - i)
        x[i:i + m] += lowpass(rng.standard_normal(m), 0.5) * env_exp(m, 30) * 0.55
    write_wav(out / "skeleton_ambient.wav", x)

    n = int(0.4 * SR)  # spider hiss
    x = highpass_band(rng.standard_normal(n), 0.35, 0.75) * env_exp(n, 9) * 0.30
    write_wav(out / "spider_ambient.wav", x)

    n = int(0.55 * SR)  # cow moo: rising/falling low tone
    t = np.arange(n) / SR
    f = 140 + 55 * np.sin(np.pi * t / (n / SR))
    x = np.sign(np.sin(2 * np.pi * np.cumsum(f) / SR)) * 0.20 * env_exp(n, 6)
    x = lowpass(x, 0.14)
    write_wav(out / "cow_voice.wav", x)

    n = int(0.30 * SR)  # pig oink: short nasal grunt
    t = np.arange(n) / SR
    f = 240 - 90 * (t / (n / SR))
    x = np.sign(np.sin(2 * np.pi * np.cumsum(f) / SR)) * 0.25 * env_exp(n, 18)
    x = lowpass(x, 0.30)
    write_wav(out / "pig_voice.wav", x)

    n = int(0.25 * SR)  # chicken cluck
    t = np.arange(n) / SR
    f = 700 + 250 * np.sin(2 * np.pi * 14 * t)
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * env_exp(n, 26) * 0.30
    for k in range(3):  # broken into clucks
        i = int((0.02 + 0.08 * k) * SR)
        m = int(0.05 * SR)
        x[i:i + m] *= 0.4
    write_wav(out / "chicken_voice.wav", x)

    n = int(0.5 * SR)  # sheep baa: wobbling mid tone
    t = np.arange(n) / SR
    f = 330 + 40 * np.sin(2 * np.pi * 7 * t)
    x = np.sign(np.sin(2 * np.pi * np.cumsum(f) / SR)) * 0.18 * env_exp(n, 8)
    x = lowpass(x, 0.35)
    write_wav(out / "sheep_voice.wav", x)

    n = int(2.4 * SR)  # day ambience: sparse bird chirps
    x = np.zeros(n)
    for k in range(5):
        i = int(rng.integers(0, n - int(0.3 * SR)))
        m = int(0.14 * SR)
        t2 = np.arange(m) / SR
        f = 2200 + rng.uniform(-300, 500) + 800 * np.sin(2 * np.pi * rng.uniform(14, 22) * t2)
        chirp = np.sin(2 * np.pi * np.cumsum(f) / SR) * env_exp(m, 28) * 0.16
        x[i:i + m] += chirp
        x[i + m:i + m + 60] += rng.standard_normal(min(60, n - i - m)) * 0.03
    write_wav(out / "amb_day.wav", x)

    n = int(2.8 * SR)  # night ambience: cricket chirp train
    x = np.zeros(n)
    for k in range(4):
        i = int(rng.integers(0, max(1, n - int(0.6 * SR))))
        for j in range(5):
            s = i + int(j * 0.055 * SR)
            m = int(0.028 * SR)
            if s + m >= n: break
            f = 4200 + rng.uniform(-200, 200)
            t2 = np.arange(m) / SR
            x[s:s + m] += np.sin(2 * np.pi * f * t2) * env_exp(m, 60) * 0.10
    write_wav(out / "amb_night.wav", x)

    n = int(3.0 * SR)  # cave ambience: deep drone + a water drip
    t = np.arange(n) / SR
    x = np.sin(2 * np.pi * 55 * t) * 0.05 + np.sin(2 * np.pi * 82 * t) * 0.03
    x *= 0.6 + 0.4 * np.sin(2 * np.pi * 0.15 * t)
    i = int(rng.integers(int(0.5 * SR), n - int(0.2 * SR)))
    m = int(0.16 * SR)
    t2 = np.arange(m) / SR
    f = 900 - 500 * t2 / (m / SR)
    x[i:i + m] += np.sin(2 * np.pi * np.cumsum(f) / SR) * env_exp(m, 22) * 0.20
    write_wav(out / "amb_cave.wav", x)

    n = int(0.09 * SR)  # item pickup: bright pop
    t = np.arange(n) / SR
    x = np.sin(2 * np.pi * 660 * t) * env_exp(n, 90) * 0.5
    x += np.sin(2 * np.pi * 990 * t) * env_exp(n, 110) * 0.3
    write_wav(out / "pickup.wav", x)

    n = int(0.3 * SR)  # eat: three crunches
    x = np.zeros(n)
    for k in (0, int(0.1 * SR), int(0.2 * SR)):
        m = int(0.05 * SR)
        x[k:k + m] += lowpass(rng.standard_normal(m), 0.18) * env_exp(m, 50) * 1.5
    write_wav(out / "eat.wav", x)

    n = int(0.4 * SR)  # splash
    x = lowpass(rng.standard_normal(n), 0.3) * env_exp(n, 10)
    x[:int(0.05 * SR)] *= np.linspace(0.2, 1.0, int(0.05 * SR))
    write_wav(out / "splash.wav", x * 0.8)


def note(midi):
    return 440.0 * 2 ** ((midi - 69) / 12)


def chord_pad(freqs, dur, seed, brightness):
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed)
    out = np.zeros(n)
    for f in freqs:
        for mult, amp in ((1, 1.0), (2, 0.30 * brightness), (3, 0.12 * brightness)):
            det = 1.0 + rng.uniform(-0.0012, 0.0012)
            out += amp * np.sin(2 * np.pi * f * mult * det * t + rng.uniform(0, 6.28))
    out /= (len(freqs) * 1.9)
    return out


def make_track(path, chords, chord_dur, brightness, seed, gain=0.8):
    parts = []
    for i, ch in enumerate(chords):
        pad = chord_pad([note(m) for m in ch], chord_dur, seed + i, brightness)
        n = len(pad)
        # slow breathing envelope, zero at both ends -> seamless looping
        env = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n) / n)
        parts.append(pad * env)
    x = np.concatenate(parts)
    # gentle stereo-less noise floor for air
    rng = np.random.default_rng(seed)
    x += lowpass(rng.standard_normal(len(x)), 0.02) * 0.012
    write_wav(path, x, gain)
    return x


def gen_music():
    m = A / "music"
    day = [
        (60, 64, 67), (57, 60, 64), (53, 57, 60), (55, 59, 62),
        (60, 64, 67), (57, 60, 64), (53, 57, 60), (55, 59, 62),
    ]
    night = [
        (45, 48, 52), (41, 45, 48), (43, 47, 50), (40, 43, 48),
        (45, 48, 52), (41, 45, 48), (43, 47, 50), (40, 43, 48),
    ]
    menu = [
        (53, 57, 60), (55, 59, 62), (48, 52, 55), (53, 57, 60),
        (53, 57, 60), (55, 59, 62), (48, 52, 55), (53, 57, 60),
    ]
    make_track(m / "day.wav", day, 7.0, 0.85, 501, 0.75)
    make_track(m / "night.wav", night, 9.0, 0.30, 502, 0.7)
    make_track(m / "menu.wav", menu, 8.0, 0.55, 503, 0.7)


# ----------------------------------------------------------------------------
# example mod textures
# ----------------------------------------------------------------------------

def gen_example_mod_textures():
    mod = ROOT / "mods" / "example_mod" / "assets" / "textures"
    mod.mkdir(parents=True, exist_ok=True)

    a = noise_fill((112, 132, 156), 12, 601)
    rng = random.Random(602)
    for _ in range(14):
        x, y = rng.randrange(16), rng.randrange(16)
        a[y, x, :3] = (198, 226, 240)
    to_img(a).save(mod / "frost_stone.png")

    a = noise_fill((96, 118, 142), 7, 603)
    for y in (4, 5, 10, 11):
        a[y, :, :3] = (72, 92, 116)
    for bx, y0 in ((0, 0), (8, 0), (4, 6), (12, 6)):
        for y in range(y0 + 6, min(16, y0 + 12)):
            a[y, min(15, bx), :3] = (72, 92, 116)
    to_img(a).save(mod / "frost_brick.png")
    print(f"wrote example mod textures in {mod}")


def main():
    for sub in ("textures", "gui", "environment", "audio", "music"):
        (A / sub).mkdir(parents=True, exist_ok=True)
    build_atlas()
    gen_entity_atlas()
    gui_button(); gui_panel(); gui_slot(False); gui_slot(True); gui_logo()
    gui_heart(True); gui_heart(False); gui_hunger(True); gui_hunger(False)
    gui_crosshair()
    env_sun(); env_moon(); env_clouds()
    gen_sounds()
    gen_music()
    gen_example_mod_textures()
    print("all assets generated.")


if __name__ == "__main__":
    main()
