/* audio.c — graceful audio: works without a device, per-bus volumes,
 * pitch variation, distance attenuation and repeat cooldowns. */
#include "wl/audio.h"
#include "wl/log.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

/* minimum seconds between two plays of the same sound id */
#define WL_SND_COOLDOWN 0.045f

static double s_last_play[WL_SND_COUNT];

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

static bool snd_ready(WlAudio *a, WlSndId id)
{
    if (!a || !a->enabled || !a->assets) return false;
    if (id < 0 || id >= WL_SND_COUNT) return false;
    double now = GetTime();
    if (now - s_last_play[id] < WL_SND_COOLDOWN) return false;
    s_last_play[id] = now;
    return true;
}

static void play_bus(WlAudio *a, WlSndId id, float pitch_var, float vol_mult)
{
    if (!snd_ready(a, id)) return;
    if (!wl_assets_snd_ok(a->assets, id)) return; /* missing asset: silent */
    Sound s = wl_assets_snd(a->assets, id);
    float pitch = 1.0f;
    if (pitch_var > 0.0f)
        pitch += ((float)rand() / (float)RAND_MAX * 2.0f - 1.0f) * pitch_var;
    if (pitch < 0.25f) pitch = 0.25f;
    if (pitch > 4.0f) pitch = 4.0f;
    SetSoundPitch(s, pitch);
    SetSoundVolume(s, a->vol_master * a->vol_sfx * vol_mult);
    PlaySound(s);
}

void wl_audio_play(WlAudio *a, WlSndId id)
{
    play_bus(a, id, 0.0f, 1.0f);
}

void wl_audio_play_v(WlAudio *a, WlSndId id, float pitch_var)
{
    play_bus(a, id, pitch_var, 1.0f);
}

void wl_audio_play_at(WlAudio *a, WlSndId id, Vector3 pos, Vector3 ear,
                      float max_dist)
{
    float dx = pos.x - ear.x, dy = pos.y - ear.y, dz = pos.z - ear.z;
    float d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d >= max_dist) return; /* too far to be audible at all */
    float vol = 1.0f - d / max_dist;
    vol *= vol; /* squared falloff feels more natural */
    if (vol < 0.03f) return;
    play_bus(a, id, 0.06f, vol);
}

void wl_audio_ambient(WlAudio *a, WlSndId id)
{
    if (!snd_ready(a, id)) return;
    if (!wl_assets_snd_ok(a->assets, id)) return;
    Sound s = wl_assets_snd(a->assets, id);
    SetSoundPitch(s, 1.0f);
    SetSoundVolume(s, a->vol_master * a->vol_ambient);
    PlaySound(s);
}

void wl_audio_stop_sfx(WlAudio *a)
{
    if (!a || !a->enabled || !a->assets) return;
    for (int i = 0; i < (int)WL_SND_COUNT; i++) {
        if (wl_assets_snd_ok(a->assets, (WlSndId)i))
            StopSound(wl_assets_snd(a->assets, (WlSndId)i));
    }
    memset(s_last_play, 0, sizeof(s_last_play));
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

void wl_audio_update(WlAudio *a, float dt, uint32_t world_ticks, bool in_game,
                     bool underground)
{
    if (!a->enabled || !a->assets) return;

    if (in_game) {
        /* night = 13500..22500 ticks, with dawn/dusk blending around it */
        bool night = world_ticks > 13500 && world_ticks < 22500;
        switch_track(a, night ? WL_MUS_NIGHT : WL_MUS_DAY);

        /* occasional surface / cave ambience on the ambient bus */
        static float s_amb_timer = 6.0f;
        s_amb_timer -= dt;
        if (s_amb_timer <= 0.0f) {
            if (underground) {
                wl_audio_ambient(a, WL_SND_AMB_CAVE);
                s_amb_timer = 9.0f + 8.0f * ((float)rand() / (float)RAND_MAX);
            } else if (night) {
                wl_audio_ambient(a, WL_SND_AMB_NIGHT);
                s_amb_timer = 7.0f + 9.0f * ((float)rand() / (float)RAND_MAX);
            } else {
                wl_audio_ambient(a, WL_SND_AMB_DAY);
                s_amb_timer = 10.0f + 14.0f * ((float)rand() / (float)RAND_MAX);
            }
        }
    } else {
        switch_track(a, WL_MUS_MENU);
    }

    if (a->current_track >= 0 && wl_assets_music_ok(a->assets, (WlMusId)a->current_track)) {
        Music m = wl_assets_music(a->assets, (WlMusId)a->current_track);
        UpdateMusicStream(m);
    }
}
