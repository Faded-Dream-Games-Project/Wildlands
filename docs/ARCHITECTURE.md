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
  |       world_update_drops (drop physics + player pickup)
  |       world_update_entities (mob/animal AI, arrows, spawner)
  |       furnaces_update (smelting tick)
  |       renderer_begin    (camera from player, sky colors, fog)
  |       renderer_draw_world (sky -> sun/moon -> chunks -> clouds -> water)
  |       renderer_draw_drops + draw_held_item (depth-cleared, camera space)
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

## Lighting

- Per-voxel light in every chunk, one byte per voxel: high nibble
  skylight, low nibble block light (0..15 each). Derived data — torches
  and lit furnaces are blocks, so nothing light-related is saved.
- `src/world/light.c` recomputes a chunk with two BFS passes (sky
  columns seeded from the top, emitters seeded from block defs) plus
  border imports from the four side neighbors. Border checksums detect
  changes and mark neighbors light+mesh dirty so propagation crosses
  chunk boundaries.
- The mesher bakes two brightness channels into vertex colors
  (r = block light, g = skylight, both with face shade + AO); the chunk
  shader combines `max(r, g * dayAmbient)`, so torch light stays bright
  at night while the sky channel follows the day/night cycle.
- Edits mark their chunk light-dirty; the mesh step relights before
  rebuilding, so torch place/remove and block changes update the world
  within a frame.

## Entities

- `WlEntity` array in the world (like drops): zombies, skeletons,
  spiders, arrows, cows, pigs, chickens, sheep. AI per type in
  `src/gameplay/entities.c` (chase/keep-distance/wander/flee), shared
  axis-separated AABB collision with auto-jump, and a natural spawner
  that uses the lighting engine: hostiles need darkness (night surface
  or caves), animals need sunlit grass in plains/forest.
- Rendered as animated box models: per-type part tables (body, head,
  swinging legs, zombie arms held forward, spider legs, chicken wings),
  skinned from `assets/textures/entities.png` through the entity shader
  with per-entity light sampled from the voxel grid plus a hurt flash.
- Persist in `entities.dat`; arrows are transient.

## Furnaces

- Block entities keyed by position (`WlFurnace` array in the world):
  input/fuel/output stacks, burn timer and smelt progress. The tick in
  `src/gameplay/furnace.c` ignites fuel, consumes input, respects
  output stack limits and swaps the world block between furnace and
  furnace_lit (which emits light 13). Contents persist in
  `furnaces.dat`; breaking a furnace drops its stacks.

## Audio

- `src/audio/audio.c`: graceful (runs silent without a device), four
  volume buses (master / music / sfx / ambience) applied through the
  settings menu. Effects get random pitch variation, a per-sound
  45 ms repeat cooldown and optional distance attenuation
  (`wl_audio_play_at`, squared falloff to zero at max distance).
- Sounds are external wavs under `assets/audio/`, generated by
  `tools/gen_assets.py`, registered in the asset manager and replaceable
  by resource packs. Modules without audio access (world drops, the
  furnace tick) raise small event queues that the game loop drains and
  plays positionally; leaving a world stops all effects at once.

## Dropped items

- Death in survival moves every stack (hotbar, main inventory, crafting
  grid, cursor) into `WlDrop` entities at the player position, cleared
  only after the entity exists — a full drop list retries the rest next
  frame, so nothing is lost or duplicated. Creative keeps the inventory.
- Drops fall with simple gravity + ground clamp, spin/bob when rendered,
  and are collected by walking within 1.4 blocks (`wl_inv_add_partial`
  keeps whatever does not fit in the world).
- Persisted as `drops.dat` (one line per stack) with each session save;
  old worlds without the file simply load zero drops.

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
