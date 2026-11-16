# Wildlands

Wildlands is an independently developed voxel sandbox game written in **pure C (C11)**
on top of [raylib](https://www.raylib.com). It implements its own voxel engine,
procedural world generation, survival and creative gameplay, persistent worlds,
a full settings system, and a native shared-library modding API — with original
assets and its own identity. Build, explore, survive, and create.

> **Status:** early in active development. See [docs/ROADMAP.md](docs/ROADMAP.md)
> for what is implemented and what is planned.

## Building

Requirements: a C compiler (gcc or clang), GNU make, X11/OpenGL development
libraries. raylib 6.2 is vendored under `third_party/raylib` and is built
automatically — no cmake, no system raylib install needed.

```sh
make            # release build -> bin/wildlands
make debug      # debug build   -> bin/wildlands_debug
make run        # build + run
make test       # build + run unit tests
make mods       # build the example mod
make clean      # remove build artifacts
```

The game creates its runtime directories (`worlds/`, `config/`, `logs/`,
`screenshots/`, `mods/`) automatically on first start.

## Controls (defaults, fully rebindable in Settings → Controls)

| Action | Key |
| --- | --- |
| Move | W A S D |
| Jump | Space |
| Sneak | Left Shift |
| Sprint | Left Ctrl |
| Attack / break | Left mouse |
| Use / place | Right mouse |
| Pick block | Middle mouse |
| Inventory | E |
| Hotbar | 1–9 or mouse wheel |
| Debug overlay | F3 |
| Screenshot | F2 |
| Pause | Esc |

## Modding

Wildlands is designed around modding. Mods are native C shared libraries with a
versioned C API; see [docs/MODDING.md](docs/MODDING.md) and the working example
in [mods/example_mod/](mods/example_mod/).

## Project layout

```
src/            game code (core, world, generation, player, gameplay, ui, ...)
include/wl/     public headers
assets/         all external assets (textures, gui, fonts, audio, shaders)
third_party/    vendored raylib 6.2 + build sysroot headers
worlds/         saved worlds (created at runtime)
mods/           installed mods + example mod source
config/         options.txt, keybindings.txt (created at runtime)
tools/          development-time asset generation scripts (not part of the game)
tests/          unit tests
```

## License

MIT — see [LICENSE](LICENSE). raylib is zlib-licensed, fonts are OFL-licensed;
their license texts are kept alongside them.
