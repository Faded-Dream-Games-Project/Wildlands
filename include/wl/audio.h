/* audio.h — music/sfx playback with per-bus volume control. */
#ifndef WL_AUDIO_H
#define WL_AUDIO_H

#include "wl/wl_common.h"
#include "wl/assets.h"
#include "raylib.h" /* Vector3 */

typedef struct WlAudio {
    WlAssets *assets;
    bool enabled;        /* false when the audio device is unavailable */
    float vol_master, vol_music, vol_sfx, vol_ambient;
    int current_track;   /* WlMusId or -1 */
    double track_fade;   /* seconds of current track played (for switching) */
} WlAudio;

/* Initializes the audio device. On failure every call becomes a no-op and
 * the game continues silently. */
bool wl_audio_init(WlAudio *a, WlAssets *assets);
void wl_audio_shutdown(WlAudio *a);

/* Call once per frame: streams music, switches tracks and triggers the
 * day/night/cave ambience. underground=true plays cave ambience instead
 * of surface day/night sounds. */
void wl_audio_update(WlAudio *a, float dt, uint32_t world_ticks, bool in_game,
                     bool underground);

void wl_audio_set_volumes(WlAudio *a, float master, float music, float sfx, float ambient);

/* Plain play on the sfx bus. Repeats inside the cooldown window are
 * dropped so rapid events cannot stack into a wall of noise. */
void wl_audio_play(WlAudio *a, WlSndId id);
/* Same, with random pitch variation +/- pitch_var (0.05 = +-5%). */
void wl_audio_play_v(WlAudio *a, WlSndId id, float pitch_var);
/* Positional play: volume falls off linearly to zero at max_dist from
 * the ear position, plus a little pitch variation. */
void wl_audio_play_at(WlAudio *a, WlSndId id, Vector3 pos, Vector3 ear,
                      float max_dist);
/* Ambient one-shots (birdsong, crickets, cave drone) on the ambient bus. */
void wl_audio_ambient(WlAudio *a, WlSndId id);

/* Stops every sound effect at once (world exit, death screen). */
void wl_audio_stop_sfx(WlAudio *a);
void wl_audio_stop_music(WlAudio *a);

#endif /* WL_AUDIO_H */
