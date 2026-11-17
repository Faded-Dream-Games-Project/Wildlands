# Wildlands Architecture

A short map of the codebase for contributors. The game is pure C11 on
raylib 6.2; every module lives in `src/<area>/` with its header in
`include/wl/`. No module reaches into another module's internals — all
cross-module traffic goes through headers.

```
main.c                 thin entry point (init -> frame loop -> shutdown)
core/game.c            WlGame context, state machine, world session
core/log.c             thread-safe logging (console + per-session file)
core/config.c          options.txt (table-driven load/save/validate)
core/keybinds.c        rebindable actions, key<->name mapping
core/paths.c           project-root resolution, runtime dirs, log pruning
core/util.c            math, strings, atomic file writes, RLE, str builder
assets/asset_manager.c textures/fonts/sounds/music/shaders + atlas compose
world/block.c          block registry (base + mods)
world/item.c           item registry (mirrored block items + pure items)
world/chunk.c          16x16x128 chunk storage, top-height cache
world/world.c          chunk hash map, streaming, generation thread
world/mesher.c         chunk meshing: face culling, AO, baked shading
generation/noise.c     seeded value noise, fBm, splitmix64 RNG
generation/worldgen.c  biomes, terrain, rivers, caves, ores, trees, decor
player/player.c        physics, collision, survival simulation
player/interact.c      voxel DDA raycast, mining, placing, eating
gameplay/inventory.c   slots, stacking, pocket crafting grid
gameplay/crafting.c    recipe registry, shaped/shapeless matcher
render/renderer.c      camera, sky, frustum culling, chunk/cloud/water passes
save/world_store.c     level.dat + RLE chunk files (atomic writes)
save/worlds.c          world list, create/delete, session save/load
ui/ui.c                immediate-mode widget toolkit + item icon rendering
ui/hud.c               crosshair, hotbar, health/hunger/air, debug overlay
ui/inv_ui.c            inventory + crafting overlay (drag & drop)
menus/*.c              main, world select/create, settings, pause, mods
audio/audio.c          music/sfx buses, graceful no-device operation
modding/mod_loader.c   manifest parsing, deps, dlopen, API vtable
```

## Data flow per frame

```
wl_game_run_frame
  |-- wl_ui_begin           (mouse/keyboard snapshot for widgets)
  |-- state dispatch:
  |     PLAYING:
  |       player_update     (input -> physics -> survival stats)
  |       interact_update   (raycast, mining progress, placement)
  |       world_update      (stream chunks, adopt generated, budget meshes)
  |       renderer_begin    (camera from player, sky colors, fog)
  |       renderer_draw_world (sky -> sun/moon -> chunks -> clouds -> water)
  |       hud + overlays
  |-- wl_ui_end
```

## Threading model

- **Main thread**: everything except terrain generation.
- **Generation worker** (one thread, created per world): pops chunk
  coordinates from a queue, fills plain `uint8_t` buffers through the
  stateless worldgen, pushes results back through a second queue. It never
  touches chunks, the GPU or the registries.
- Fallback: if `pthread_create` fails, generation runs inline on the main
  thread with a per-frame budget. Behavior is identical either way.
- All worldgen randomness is derived from `(seed, coordinates)` — chunk
  generation is deterministic regardless of order or thread.

## Memory ownership

- `WlAssets` owns every GPU resource (textures, fonts, sounds, music,
  shaders). Renderer *materials* are plain structs that reference
  assets-owned resources and are never unloaded individually.
- Chunk meshes: the mesher transfers its CPU buffers into raylib `Mesh`
  structs at upload time; `wl_chunk_free` unloads them.
- The mod loader copies every registry string into its own arena, so
  registries never point into unloaded libraries.

## Rendering details

- One draw call per chunk mesh; two meshes per chunk (opaque+cutout, water).
- Vertex format: `float3 pos, float2 uv, ubyte4 color` — shading (face
  direction × ambient occlusion × skylight depth) is baked per vertex.
- Custom GLSL shaders (`assets/shaders/chunk_330.*` with a GLSL-120
  fallback) add distance fog and a day/night light multiplier. Uniforms
  come from raylib's standard material slots (`matModel`, `matView`, ...).
- Frustum culling: planes extracted from the view-projection matrix
  (Gribb-Hartmann), positive-vertex AABB test per chunk.
- Water is a translucent second pass with depth writes disabled; cutout
  blocks (plants, leaves, glass) live in the opaque pass with alpha-discard.
- The frame's color *and depth* buffers are cleared at the top of
  `wl_renderer_draw_world` (raylib `ClearBackground` clears both). The sky
  is then painted as a 2D gradient quad, which writes no depth — so without
  the clear the depth buffer keeps the previous frame's terrain depths and
  any fragment that becomes farther than its stale value is discarded by
  the `GL_LEQUAL` test once the camera moves. That was the "blocks turn
  white / untextured after moving the mouse" bug; `WL_NO_DEPTHCLEAR=1`
  restores the old behaviour for A/B regression runs
  (`tools/repro_render.sh`, `tools/verify_battery.sh`).

## Persistence

- `worlds/<dir>/level.dat` — world metadata (text key=value).
- `worlds/<dir>/player.dat` — player state incl. the full inventory.
- `worlds/<dir>/chunks/c.<x>.<z>.bin` — **only chunks the player modified**,
  RLE-compressed; everything else regenerates from the seed.
- Every write is `file.tmp` + `fsync` + `rename` — a crash mid-save never
  corrupts an existing world.

## Save-format contracts (do not break)

- Base block ids 0..28 and their mirrored item ids (see `include/wl/block.h`).
- Atlas tile layout 0..55 (`include/wl/tiles.h`, verified against the
  generator and by unit tests).
- `level.dat` / chunk file formats carry `format_version` and are validated
  on load.
