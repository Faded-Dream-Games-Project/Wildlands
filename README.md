# Wildlands

Wildlands is an independently developed voxel sandbox game written in **pure
C (C11)** on top of [raylib](https://www.raylib.com) 6.2. It implements its
own voxel engine, deterministic procedural world generation, survival and
creative gameplay, persistent worlds, a complete settings system, and a
native shared-library modding API — with original assets and its own
identity. Build, explore, survive, and create.

> Status and plans: [docs/ROADMAP.md](docs/ROADMAP.md) — what is done and
> what is not, honestly listed.

![screenshot](screenshots/README.md-screenshot)

## Features

- **Voxel engine** — chunk streaming with a background generation thread,
  face culling, per-vertex ambient occlusion and skylight shading, frustum
  culling, custom GLSL shaders with fog and day/night lighting, translucent
  water, clouds, sun and moon.
- **Procedural worlds** — deterministic seeds; oceans, beaches, plains,
  forests, deserts, mountains and snowfields; rivers, caves, four ore
  types, trees, cacti and flowers; superflat world type.
- **Survival & creative** — mining with tool tiers and durability, block
  placement rules, health/hunger/drowning, fall damage, death & respawn,
  20-minute day/night cycle, creative flight.
- **Inventory & crafting** — 36-slot inventory with drag & drop, 2×2 pocket
  grid and 3×3 crafting table, shaped (mirrored), shapeless and
  ingredient-group recipes.
- **Persistent worlds** — create/select/delete with confirmation, atomic
  crash-safe saves, autosave, full inventory persistence.
- **Full settings** — video / audio / controls / gameplay tabs, rebindable
  keys, live-applied changes, persisted to `config/`.
- **Modding from day one** — native C mods with a versioned API,
  dependency resolution, enable/disable management, working example mod.
  See [docs/MODDING.md](docs/MODDING.md).
- **Original assets** — every texture, sound and music track is generated
  for this project (see `tools/gen_assets.py`); fonts are OFL-licensed.

## Building

Requirements: a C compiler (gcc or clang), GNU make. raylib 6.2 is vendored
under `third_party/` and builds automatically — no cmake, no system raylib,
no root access needed.

```sh
make            # release build  -> bin/wildlands
make debug      # debug build    -> bin/wildlands_debug
make asan       # AddressSanitizer build
make run        # build + run (auto-starts Xvfb when no display is present)
make test       # build + run the unit tests (176 checks)
make selftest   # build + run the in-game runtime self test
make mods       # build the example mod (mods/example_mod/mod.so)
make assets     # regenerate original assets (dev only, needs Python+Pillow)
make clean      # remove build artifacts
```

Startup is fully staged and logged with per-stage timings; every failure
(display missing, GL context failed, assets missing) prints an actionable
message instead of crashing. Frame pacing defaults to Auto (60 fps cap;
VSync paces when it engages) and is configurable up to 240 in Settings.

The game creates its runtime directories (`worlds/`, `mods/`, `config/`,
`logs/`, `screenshots/`) automatically on first start.

Menus with more content than fits (settings at small window sizes or UI
scales, world creation) scroll with the mouse wheel; open dropdown lists
render above all other controls and swallow clicks inside them. The UI
runtime test (`./bin/wildlands --uitest settings` / `--uitest create`)
drives the real menus with a scripted pointer and verifies focus, typing,
scrolling, dropdown layering and world creation end to end.

### Command line

```
wildlands [--world <name>] [--seed <n>] [--gamemode survival|creative]
          [--type standard|superflat] [--width <w>] [--height <h>]
          [--frames <n>] [--screenshot <path>]
```

`--frames` runs a headless-friendly demo (used by the automated smoke
tests) that exits after N played frames.

## Controls (defaults — rebindable in Settings → Controls)

| Action | Key |
| --- | --- |
| Move | W A S D |
| Jump | Space (double-tap: toggle flight in creative) |
| Sneak | Left Shift |
| Sprint | Left Ctrl |
| Attack / mine | Left mouse (hold) |
| Use / place / eat | Right mouse |
| Pick block | Middle mouse |
| Inventory | E |
| Hotbar | 1–9 or mouse wheel |
| Move stack | Shift + click · Split / place one | Right click |
| Debug overlay | F3 |
| Screenshot | F2 |
| Pause | Esc |

## Project layout

```
src/            game code (core, world, generation, player, gameplay,
                render, save, ui, menus, audio, modding)
include/wl/     public headers
assets/         all external assets (textures, gui, fonts, audio, shaders)
third_party/    vendored raylib 6.2 + build sysroot
worlds/         saved worlds (created at runtime)
mods/           installed mods + example mod source
config/         options.txt, keybindings.txt (created at runtime)
tools/          development-time asset generator (not part of the game)
tests/          unit tests
docs/           MODDING.md, ARCHITECTURE.md, ROADMAP.md
```

More detail: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Modding

Mods are native C shared libraries with a versioned, raylib-free API:

```c
int wl_mod_main(const WlApi *api, WlModInfo *out_info);
```

Full guide: [docs/MODDING.md](docs/MODDING.md) · working example:
[mods/example_mod/](mods/example_mod/) (built by `make mods`, managed via
the in-game Mods menu).

## License

MIT — see [LICENSE](LICENSE). raylib is zlib-licensed, fonts are
OFL-licensed; their license texts are kept alongside them.
