/* entities.h — hostile mobs, passive animals, projectiles.
 *
 * Entities live in the world (like dropped items), update on the main
 * thread each frame and persist to entities.dat. Rendering happens in the
 * renderer via per-type part meshes skinned from the entity atlas. */
#ifndef WL_ENTITIES_H
#define WL_ENTITIES_H

#include "wl/wl_common.h"
#include "raylib.h"

struct WlWorld;
struct WlPlayer;
struct WlConfig;
struct WlAudio;

typedef enum {
    WL_ENT_NONE = 0,
    WL_ENT_ZOMBIE,
    WL_ENT_SKELETON,
    WL_ENT_SPIDER,
    WL_ENT_ARROW,
    WL_ENT_COW,
    WL_ENT_PIG,
    WL_ENT_CHICKEN,
    WL_ENT_SHEEP,
    WL_ENT_TYPE_COUNT
} WlEntityType;

typedef struct WlEntity {
    bool active;
    WlEntityType type;
    Vector3 pos;         /* feet center (like the player) */
    Vector3 vel;
    float yaw;           /* facing, radians; 0 looks toward -Z */
    float health;
    float anim_phase;    /* walk cycle accumulator */
    float attack_cd;     /* melee cooldown */
    float shoot_cd;      /* skeleton bow cooldown */
    float hurt_flash;    /* seconds of red tint left */
    float wander_timer;  /* animal: until next heading change */
    float despawn_timer; /* far-away despawn accumulator */
    float age;           /* seconds alive */
    float live_ticks;    /* deterministic time source for idle anims */
    bool provoked;       /* spider: hostile even in daylight once hurt */
    bool on_ground;
    uint32_t id;         /* stable for saves */
} WlEntity;

#define WL_ENTITIES_MAX 96

/* Keeps the world's next-entity-id counter ahead of loaded ids. */
void wl_world_note_entity_id(struct WlWorld *w, uint32_t id);

/* Fills in health and defaults for a freshly placed entity. */
void wl_entity_defaults(WlEntity *e);

/* Spawns an entity at pos (feet). Returns NULL when the list is full. */
WlEntity *wl_world_spawn_entity(struct WlWorld *w, WlEntityType type, Vector3 pos);

/* Dense list of active entities (valid until the next update). */
WlEntity *wl_world_entities(struct WlWorld *w, int *count);

/* Per-frame AI, physics, combat and the natural spawner. */
void wl_world_update_entities(struct WlWorld *w, struct WlPlayer *p,
                              const struct WlConfig *cfg, struct WlAudio *audio,
                              float dt);

/* Damage + knockback from the player's attack direction. */
void wl_entity_hurt(struct WlWorld *w, WlEntity *e, float amount, Vector3 knock_dir,
                    struct WlAudio *audio);

/* First entity hit by a ray within max_dist (used for melee attacks). */
WlEntity *wl_entity_ray_hit(struct WlWorld *w, Vector3 origin, Vector3 dir,
                            float max_dist, float *hit_dist);

/* Extents for collision and hit tests. */
void wl_entity_extents(WlEntityType t, float *half_w, float *height);
bool wl_entity_type_hostile(WlEntityType t);

/* Persistence: entities.dat in the world's save dir. */
bool wl_world_save_entities(const struct WlWorld *w);
void wl_world_load_entities(struct WlWorld *w);

/* Test seams. */
void wl_entities_debug_spawn_rate(float attempts_per_sec);

#endif /* WL_ENTITIES_H */
