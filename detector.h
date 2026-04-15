/* Sound detector: consumes 2048-sample stereo windows (50% overlapped),
 * runs FFT-based analysis, and emits SoundEvent values when footsteps
 * or gunshots are detected.
 *
 * The detector is stateful: adaptive per-band noise floors, spectral
 * flux for onset detection, and per-class refractory periods.
 */
#ifndef SOUND_OVERLAY_DETECTOR_H
#define SOUND_OVERLAY_DETECTOR_H

#include "audio.h"
#include "fft.h"

typedef enum {
    SE_FOOTSTEP = 0,
    SE_GUNSHOT  = 1,
} SoundEventKind;

typedef struct {
    SoundEventKind kind;
    float  pan;        /* -1 full left .. +1 full right */
    float  strength;   /* arbitrary >=1 scale for rendering size */
    double timestamp;  /* QueryPerformanceCounter seconds */
} SoundEvent;

typedef struct {
    FFT    fft;
    float *window;        /* Hann, length AUDIO_FFT_FRAMES */
    float *scr_re;
    float *scr_im;
    float *mag_prev;
    float *mag_curr;      /* length AUDIO_FFT_FRAMES/2 + 1 */
    size_t bins;

    /* Adaptive per-band noise floors (mean-square magnitude). */
    float nf_low;     /* 40-220 Hz     */
    float nf_mid;     /* 300-1200 Hz   */
    float nf_high;    /* 1500-6000 Hz  */
    float nf_ultra;   /* 6000-12000 Hz */
    float nf_flux;    /* spectral flux */

    double last_footstep_s;
    double last_gunshot_s;

    float sensitivity;      /* 0.5 .. 2.0, multiplies thresholds */

    unsigned frame_count;   /* warmup counter so noise floors stabilize */

    double qpc_freq;
} SoundDetector;

int  detector_init(SoundDetector *d, float sensitivity);
void detector_free(SoundDetector *d);
void detector_set_sensitivity(SoundDetector *d, float s);

/* Analyze one window. Writes up to `max_events` events into `events`
 * and returns how many were written (0, 1 or 2). */
int detector_analyze(SoundDetector *d,
                     const float *left,
                     const float *right,
                     SoundEvent *events,
                     int max_events);

#endif
