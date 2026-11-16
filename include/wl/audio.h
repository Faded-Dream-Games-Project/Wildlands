/* audio.h — music/sfx playback with per-bus volume control. */
#ifndef WL_AUDIO_H
#define WL_AUDIO_H

#include "wl/wl_common.h"
#include "wl/assets.h"

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

/* Call once per frame: streams music and switches tracks. */
void wl_audio_update(WlAudio *a, float dt, uint32_t world_ticks, bool in_game);

void wl_audio_set_volumes(WlAudio *a, float master, float music, float sfx, float ambient);
void wl_audio_play(WlAudio *a, WlSndId id);
void wl_audio_stop_music(WlAudio *a);

#endif /* WL_AUDIO_H */
