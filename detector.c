#include "detector.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double now_seconds(const SoundDetector *d) {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / d->qpc_freq;
}

int detector_init(SoundDetector *d, float sensitivity) {
    if (!d) return -1;
    memset(d, 0, sizeof(*d));
    if (fft_init(&d->fft, AUDIO_FFT_FRAMES) != 0) return -1;

    size_t n = AUDIO_FFT_FRAMES;
    d->bins   = n / 2 + 1;
    d->window = (float *)malloc(sizeof(float) * n);
    d->scr_re = (float *)malloc(sizeof(float) * n);
    d->scr_im = (float *)malloc(sizeof(float) * n);
    d->mag_prev = (float *)calloc(d->bins, sizeof(float));
    d->mag_curr = (float *)calloc(d->bins, sizeof(float));
    if (!d->window || !d->scr_re || !d->scr_im || !d->mag_prev || !d->mag_curr) {
        detector_free(d);
        return -1;
    }

    /* Hann window. */
    for (size_t i = 0; i < n; ++i) {
        d->window[i] = 0.5f * (1.0f - cosf((float)(2.0 * M_PI * (double)i / (double)(n - 1))));
    }

    d->nf_low = d->nf_mid = d->nf_high = d->nf_ultra = 1e-8f;
    d->nf_flux = 1e-6f;
    d->sensitivity = sensitivity > 0.05f ? sensitivity : 1.0f;

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    d->qpc_freq = (double)f.QuadPart;
    return 0;
}

void detector_free(SoundDetector *d) {
    if (!d) return;
    fft_free(&d->fft);
    free(d->window);   d->window   = NULL;
    free(d->scr_re);   d->scr_re   = NULL;
    free(d->scr_im);   d->scr_im   = NULL;
    free(d->mag_prev); d->mag_prev = NULL;
    free(d->mag_curr); d->mag_curr = NULL;
}

void detector_set_sensitivity(SoundDetector *d, float s) {
    if (!d) return;
    if (s < 0.1f) s = 0.1f;
    if (s > 5.0f) s = 5.0f;
    d->sensitivity = s;
}

/* Mean power of bins in [f0, f1) Hz, applied to a magnitude spectrum. */
static float band_power(const float *mag, size_t bins,
                        float bin_hz, float f0, float f1) {
    size_t k0 = (size_t)(f0 / bin_hz);
    size_t k1 = (size_t)(f1 / bin_hz);
    if (k1 > bins) k1 = bins;
    if (k0 >= k1) return 0.0f;
    double s = 0.0;
    for (size_t k = k0; k < k1; ++k) {
        s += (double)mag[k] * (double)mag[k];
    }
    return (float)(s / (double)(k1 - k0));
}

/* Positive spectral flux in [f0, f1) Hz. */
static float band_flux(const float *prev, const float *curr, size_t bins,
                       float bin_hz, float f0, float f1) {
    size_t k0 = (size_t)(f0 / bin_hz);
    size_t k1 = (size_t)(f1 / bin_hz);
    if (k1 > bins) k1 = bins;
    if (k0 >= k1) return 0.0f;
    double s = 0.0;
    for (size_t k = k0; k < k1; ++k) {
        float d = curr[k] - prev[k];
        if (d > 0) s += d;
    }
    return (float)(s / (double)(k1 - k0));
}

int detector_analyze(SoundDetector *d,
                     const float *left,
                     const float *right,
                     SoundEvent *events,
                     int max_events) {
    if (!d || !left || !right || !events || max_events <= 0) return 0;

    size_t n = AUDIO_FFT_FRAMES;

    /* Mono mix, windowed. */
    for (size_t i = 0; i < n; ++i) {
        d->scr_re[i] = 0.5f * (left[i] + right[i]) * d->window[i];
        d->scr_im[i] = 0.0f;
    }
    /* FFT directly (avoid extra copy in fft_magnitude_real). */
    fft_forward(&d->fft, d->scr_re, d->scr_im);

    float *prev = d->mag_prev;
    float *curr = d->mag_curr;
    for (size_t k = 0; k < d->bins; ++k) {
        float r = d->scr_re[k];
        float ii = d->scr_im[k];
        curr[k] = sqrtf(r * r + ii * ii);
    }

    float bin_hz = (float)AUDIO_SAMPLE_RATE / (float)n;

    float p_low   = band_power(curr, d->bins, bin_hz,   40.0f,   220.0f);
    float p_mid   = band_power(curr, d->bins, bin_hz,  300.0f,  1200.0f);
    float p_high  = band_power(curr, d->bins, bin_hz, 1500.0f,  6000.0f);
    float p_ultra = band_power(curr, d->bins, bin_hz, 6000.0f, 12000.0f);

    /* Spectral flux focused on gunshot / transient range. */
    float flux_high = band_flux(prev, curr, d->bins, bin_hz, 1500.0f, 8000.0f);
    float flux_low  = band_flux(prev, curr, d->bins, bin_hz,   40.0f,  220.0f);

    /* Adaptive noise floors. Alpha is slow so we don't eat transients. */
    const float a = 0.02f;
    d->nf_low    = (1.0f - a) * d->nf_low    + a * p_low;
    d->nf_mid    = (1.0f - a) * d->nf_mid    + a * p_mid;
    d->nf_high   = (1.0f - a) * d->nf_high   + a * p_high;
    d->nf_ultra  = (1.0f - a) * d->nf_ultra  + a * p_ultra;
    d->nf_flux   = (1.0f - a) * d->nf_flux   + a * flux_high;

    float r_low    = p_low    / (d->nf_low    + 1e-12f);
    float r_mid    = p_mid    / (d->nf_mid    + 1e-12f);
    float r_high   = p_high   / (d->nf_high   + 1e-12f);
    float r_ultra  = p_ultra  / (d->nf_ultra  + 1e-12f);
    float r_fluxH  = flux_high/ (d->nf_flux   + 1e-12f);

    /* Directional estimate from energy-weighted L/R difference on the
     * windowed signals. */
    double sl = 0.0, sr = 0.0;
    for (size_t i = 0; i < n; ++i) {
        float wl = left[i]  * d->window[i];
        float wr = right[i] * d->window[i];
        sl += (double)wl * (double)wl;
        sr += (double)wr * (double)wr;
    }
    float rms_l = (float)sqrt(sl / (double)n) + 1e-9f;
    float rms_r = (float)sqrt(sr / (double)n) + 1e-9f;
    float pan = (rms_r - rms_l) / (rms_r + rms_l);
    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;

    int emitted = 0;
    double now = now_seconds(d);
    float s = d->sensitivity;

    /* Suppress classification until the adaptive floors have had time
     * to converge; with alpha=0.02 that's a few dozen frames. */
    d->frame_count++;
    if (d->frame_count < 40) {
        float *tmp_w = d->mag_prev;
        d->mag_prev = d->mag_curr;
        d->mag_curr = tmp_w;
        return 0;
    }

    /* Gunshot criteria:
     *   1. Huge positive spectral flux in high band (broadband onset)
     *   2. High-band energy significantly above its floor
     *   3. Ultra-high band also elevated (crack transient)
     *   4. Minimum absolute loudness gate
     */
    if (emitted < max_events
        && r_fluxH > 6.0f * s
        && r_high  > 4.0f * s
        && r_ultra > 2.5f * s
        && (p_high + p_ultra) > 1e-4f
        && (now - d->last_gunshot_s) > 0.13) {
        SoundEvent *e = &events[emitted++];
        e->kind = SE_GUNSHOT;
        e->pan = pan;
        float mag = (r_fluxH / (6.0f * s)) * 0.5f + (r_high / (4.0f * s)) * 0.5f;
        if (mag < 1.0f) mag = 1.0f;
        if (mag > 3.0f) mag = 3.0f;
        e->strength = mag;
        e->timestamp = now;
        d->last_gunshot_s = now;
    }

    /* Footstep criteria:
     *   1. Low-band power well above floor
     *   2. Low-band flux positive (onset), not just sustained rumble
     *   3. Low band dominates over high band (not a gunshot / music hit)
     *   4. Mid band moderate (not a synth bass pad that's already loud)
     */
    if (emitted < max_events
        && r_low > 3.5f * s
        && flux_low > 0.0f
        && p_low > 4e-5f
        && p_low > 1.4f * p_high
        && r_mid < 6.0f * s
        && (now - d->last_footstep_s) > 0.10) {
        SoundEvent *e = &events[emitted++];
        e->kind = SE_FOOTSTEP;
        e->pan = pan;
        float mag = r_low / (3.5f * s);
        if (mag < 1.0f) mag = 1.0f;
        if (mag > 3.0f) mag = 3.0f;
        e->strength = mag;
        e->timestamp = now;
        d->last_footstep_s = now;
    }

    /* Swap buffers. */
    float *tmp = d->mag_prev;
    d->mag_prev = d->mag_curr;
    d->mag_curr = tmp;

    return emitted;
}
