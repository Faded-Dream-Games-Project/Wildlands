/* audio.c — graceful audio: works without a device, per-bus volumes. */
#include "wl/audio.h"
#include "wl/log.h"

#include <string.h>

bool wl_audio_init(WlAudio *a, WlAssets *assets)
{
    memset(a, 0, sizeof(*a));
    a->assets = assets;
    a->current_track = -1;
    a->vol_master = 0.8f;
    a->vol_music = 0.6f;
    a->vol_sfx = 1.0f;
    a->vol_ambient = 0.7f;

    InitAudioDevice();
    a->enabled = IsAudioDeviceReady();
    if (!a->enabled) {
        WL_LOGW("no audio device available; running silent");
    } else {
        WL_LOGI("audio device ready");
    }
    return a->enabled;
}

void wl_audio_shutdown(WlAudio *a)
{
    if (a->enabled) {
        wl_audio_stop_music(a);
        CloseAudioDevice();
    }
    a->enabled = false;
}

void wl_audio_set_volumes(WlAudio *a, float master, float music, float sfx, float ambient)
{
    a->vol_master = master;
    a->vol_music = music;
    a->vol_sfx = sfx;
    a->vol_ambient = ambient;

    if (!a->enabled || !a->assets) return;

    for (int i = 0; i < (int)WL_SND_COUNT; i++) {
        if (wl_assets_snd_ok(a->assets, (WlSndId)i)) {
            Sound s = wl_assets_snd(a->assets, (WlSndId)i);
            SetSoundVolume(s, master * sfx);
        }
    }
    if (a->current_track >= 0 && wl_assets_music_ok(a->assets, (WlMusId)a->current_track)) {
        Music m = wl_assets_music(a->assets, (WlMusId)a->current_track);
        SetMusicVolume(m, master * music);
    }
}

void wl_audio_play(WlAudio *a, WlSndId id)
{
    if (!a || !a->enabled || !a->assets) return;
    Sound s = wl_assets_snd(a->assets, id);
    if (s.frameCount > 0) PlaySound(s);
}

void wl_audio_stop_music(WlAudio *a)
{
    if (!a || !a->enabled || !a->assets) return;
    if (a->current_track >= 0 && wl_assets_music_ok(a->assets, (WlMusId)a->current_track)) {
        Music m = wl_assets_music(a->assets, (WlMusId)a->current_track);
        StopMusicStream(m);
    }
    a->current_track = -1;
}

static void switch_track(WlAudio *a, WlMusId id)
{
    if (!a->enabled || !a->assets) return;
    if (a->current_track == (int)id) return;
    if (a->current_track >= 0 && wl_assets_music_ok(a->assets, (WlMusId)a->current_track)) {
        Music old = wl_assets_music(a->assets, (WlMusId)a->current_track);
        StopMusicStream(old);
    }
    if (wl_assets_music_ok(a->assets, id)) {
        Music m = wl_assets_music(a->assets, id);
        SetMusicVolume(m, a->vol_master * a->vol_music);
        PlayMusicStream(m);
        a->current_track = (int)id;
    } else {
        a->current_track = -1;
    }
}

void wl_audio_update(WlAudio *a, float dt, uint32_t world_ticks, bool in_game)
{
    if (!a->enabled || !a->assets) return;

    if (in_game) {
        /* night = 13500..22500 ticks, with dawn/dusk blending around it */
        bool night = world_ticks > 13500 && world_ticks < 22500;
        switch_track(a, night ? WL_MUS_NIGHT : WL_MUS_DAY);
    } else {
        switch_track(a, WL_MUS_MENU);
    }

    if (a->current_track >= 0 && wl_assets_music_ok(a->assets, (WlMusId)a->current_track)) {
        Music m = wl_assets_music(a->assets, (WlMusId)a->current_track);
        UpdateMusicStream(m);
    }
    (void)dt;
}
