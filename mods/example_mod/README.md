# Frostbite — example Wildlands mod

A minimal but complete mod: two new blocks with their own textures and a
crafting recipe, registered through the versioned C API.

```
example_mod/
  mod.wl                      manifest (id, api version, library, deps)
  example_mod.c                mod source
  assets/textures/*.png        mod-owned 16x16 textures
  mod.so                       built with `make mods` (run from the repo root)
```

Build (from the repository root):

```sh
make mods     # -> mods/example_mod/mod.so
```

The game loads every enabled mod in `mods/` at startup; check the Mods menu
or `logs/wildlands-*.log` for the load result. See [docs/MODDING.md](../../docs/MODDING.md)
for the full API documentation.
