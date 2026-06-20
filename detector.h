/* Sound detector: consumes 2048-sample stereo windows (50% overlapped),
 * runs FFT-based analysis, and emits SoundEvent values when footsteps,
 * gunshots, vehicles, or explosions are detected.
 *
 * Accepts a GameProfile for per-game frequency bands and thresholds.
 */
#ifndef SOUND_OVERLAY_DETECTOR_H
#define SOUND_OVERLAY_DETECTOR_H

#include "audio.h"
#include "fft.h"
#include "profiles.h"

typedef enum {
    SE_FOOTSTEP  = 0,
    SE_GUNSHOT   = 1,
    SE_VEHICLE   = 2,
    SE_EXPLOSION = 3,
} SoundEventKind;

#define SE_KIND_COUNT 4

typedef struct {
    SoundEventKind kind;
    float  pan;        /* -1 full left .. +1 full right (per-band) */
    float  strength;   /* arbitrary >=1 scale for rendering size */
    float  distance;   /* 0 = close (toward center) .. 1 = far (toward edge) */
    double timestamp;  /* QueryPerformanceCounter seconds */
} SoundEvent;

typedef struct {
    FFT    fft;
    float *window;        /* Hann, length AUDIO_FFT_FRAMES */
    float *l_re, *l_im;   /* left-channel spectrum scratch, length N */
    float *r_re, *r_im;   /* right-channel spectrum scratch, length N */
    float *mag_prev;
    float *mag_curr;      /* mono magnitude, length AUDIO_FFT_FRAMES/2 + 1 */
    size_t bins;

    /* Adaptive per-band noise floors. */
    float nf_foot;
    float nf_gun;
    float nf_gun_ultra;
    float nf_veh;
    float nf_expl;
    float nf_flux_gun;
    float nf_flux_foot;

    double last_foot_s;
    double last_gun_s;
    double last_veh_s;
    double last_expl_s;

    float sensitivity;

    unsigned frame_count;

    /* Active profile parameters (copied on init / profile switch). */
    float foot_lo, foot_hi;
    float gun_lo, gun_hi;
    float gun_ultra_lo, gun_ultra_hi;
    float veh_lo, veh_hi;
    float expl_lo, expl_hi;
    float foot_thresh, gun_flux_thresh, gun_power_thresh, gun_ultra_thresh;
    float veh_thresh, expl_thresh;
    float foot_cooldown, gun_cooldown, veh_cooldown, expl_cooldown;
    float nf_alpha;
    int   warmup;
    int   enable_foot, enable_gun, enable_veh, enable_expl;

    double qpc_freq;
} SoundDetector;

int  detector_init(SoundDetector *d, const GameProfile *profile, float sensitivity);
void detector_free(SoundDetector *d);
void detector_set_sensitivity(SoundDetector *d, float s);
void detector_apply_profile(SoundDetector *d, const GameProfile *p);
void detector_set_enable(SoundDetector *d, SoundEventKind kind, int on);

/* Analyze one window. Writes up to max_events events; returns count. */
int detector_analyze(SoundDetector *d,
                     const float *left,
                     const float *right,
                     SoundEvent *events,
                     int max_events);

#endif
