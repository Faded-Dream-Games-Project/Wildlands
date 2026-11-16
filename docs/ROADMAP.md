# Wildlands Roadmap

Honest status of what exists and what does not. Nothing is listed as done
unless the code for it ships in this repository.

## Implemented

### Performance & stability
- Staged startup with per-stage timings and actionable fatal errors
  (missing display, GL context failure) — no silent crashes
- Frame pacing: Auto 60 fps cap by default (VSync paces when it engages;
  the cap prevents uncapped spinning when it does not), user limit 30–240
- Runtime self test (`make selftest`): physics, raycast, mining, placing,
  session save + disk round-trip, return to menu — 11 checks
- Timing instrumentation: frame avg/worst (F3), chunk generation and
  meshing times, RSS; ASAN-clean with no leaks in game code
- Headless support: `make run` auto-starts Xvfb with software GL

### Engine
- Chunk-based voxel world (16×16×128), hash-map storage, streaming with
  configurable render distance (2–16 chunks)
- Background terrain-generation thread (with inline fallback), budgeted
  per-frame meshing
- Face culling, per-vertex ambient occlusion, skylight-depth shading,
  AO-aware quad flipping
- One-draw-call-per-chunk rendering with a custom GLSL shader (GLSL 330 +
  compat fallback), distance fog, day/night light scale
- Frustum culling, translucent water pass, scrolling cloud layer, sun/moon

### World generation
- Deterministic seeded generation (same seed → same world, any order/thread)
- Biomes: ocean, beach, plains, forest, desert, mountains, snowfield
- Rivers, beaches, frozen lakes, spaghetti + cavern caves, 4 ore types,
  oak/birch trees, cacti, flowers, tall grass, superflat world type

### Player & gameplay
- First-person controller: walk/sprint/sneak (edge-guarded), swim, creative
  flight (double-jump), swept-AABB collision, fall damage
- Voxel DDA block targeting; mining with hardness, tool kinds/tiers,
  durability; placement rules; pick-block; eating
- Survival: health, hunger/exhaustion, regeneration, starvation, drowning,
  death/respawn; difficulty levels incl. peaceful
- Creative and survival modes, day/night cycle (20 min)
- Inventory (36 slots) with stacking, drag & drop, split/merge, shift-click
- Crafting: 2×2 pocket grid + 3×3 crafting table, shaped (with mirroring),
  shapeless and ingredient-group recipes, 26 base recipes
- Falling sand/gravel is **not** implemented yet (block defs carry the
  `gravity` flag; the physics tick ships with the entity system)

### World management
- Create (name, seed, gamemode, world type), select, delete (with
  confirmation), resume; per-world play time and last-played tracking
- Atomic, crash-safe saves: level.dat, player.dat (full inventory),
  RLE-compressed modified chunks; configurable autosave

### UI & settings
- Main menu, world menus, loading screen with progress, pause menu,
  death screen, HUD (hearts, hunger, air, hotbar with icons, counts,
  durability bars, F3 debug overlay)
- Settings with four tabs — video (resolution, fullscreen, borderless,
  vsync, MSAA-restart, FPS limit, render distance, quality, FOV, view
  bobbing, brightness, UI scale), audio (4 volume buses), controls
  (rebindable keys, sensitivity, invert, smoothing), gameplay (difficulty,
  smooth camera, autosave, debug overlay, pause-on-focus-loss) — all live
  applied and persisted
- Inventory/crafting overlay with tooltips

### Audio
- 10 procedural sound effects, 3 generative ambient music tracks with
  automatic day/night/menu switching; per-bus volumes; runs silent when
  no audio device exists

### Modding (API v1)
- Native shared-library mods with manifests, dependency resolution,
  duplicate/cycle/error handling, enable/disable via the Mods menu
- C API: register blocks (auto item mirror), items, recipes, textures,
  logging — see docs/MODDING.md; working example mod included

### Infrastructure
- Vendored raylib 6.2 build (self-contained sysroot headers, no root needed)
- Original generated assets (56-tile atlas, GUI, fonts OFL, sounds, music)
- 145 unit tests (`make test`), ASAN-clean headless runs, Xvfb screenshot
  smoke tests, debug/release/asan build modes

## Not yet implemented (planned)

- Entities: mobs, animals, combat, item drop entities, falling-block physics
- Lighting: per-block light propagation (torches are decorative today)
- Furnace + smelting (glass currently has a crafting recipe instead —
  documented intentional stopgap)
- Armor, XP, enchanting-like progression
- Weather (rain/snow), biome ambience
- Additional dimensions (Nether/End analogues)
- Multiplayer (client/server architecture study)
- Mod API v2+: worldgen hooks, custom entities, sounds, commands, dimensions
- In-game recipe book / creative item browser (creative currently uses
  pick-block and the hotbar)
