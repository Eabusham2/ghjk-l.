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

static void apply_profile_params(SoundDetector *d, const GameProfile *p) {
    d->foot_lo = p->foot_lo; d->foot_hi = p->foot_hi;
    d->gun_lo  = p->gun_lo;  d->gun_hi  = p->gun_hi;
    d->gun_ultra_lo = p->gun_ultra_lo; d->gun_ultra_hi = p->gun_ultra_hi;
    d->veh_lo  = p->veh_lo;  d->veh_hi  = p->veh_hi;
    d->expl_lo = p->expl_lo; d->expl_hi = p->expl_hi;

    d->foot_thresh      = p->foot_thresh;
    d->gun_flux_thresh  = p->gun_flux_thresh;
    d->gun_power_thresh = p->gun_power_thresh;
    d->gun_ultra_thresh = p->gun_ultra_thresh;
    d->veh_thresh       = p->veh_thresh;
    d->expl_thresh      = p->expl_thresh;

    d->foot_cooldown = p->foot_cooldown;
    d->gun_cooldown  = p->gun_cooldown;
    d->veh_cooldown  = p->veh_cooldown;
    d->expl_cooldown = p->expl_cooldown;

    d->nf_alpha = p->nf_alpha;
    d->warmup   = p->warmup;

    d->enable_foot = p->enable_foot;
    d->enable_gun  = p->enable_gun;
    d->enable_veh  = p->enable_veh;
    d->enable_expl = p->enable_expl;
}

int detector_init(SoundDetector *d, const GameProfile *profile, float sensitivity) {
    if (!d) return -1;
    memset(d, 0, sizeof(*d));
    if (fft_init(&d->fft, AUDIO_FFT_FRAMES) != 0) return -1;

    size_t n = AUDIO_FFT_FRAMES;
    d->bins     = n / 2 + 1;
    d->window   = (float *)malloc(sizeof(float) * n);
    d->l_re     = (float *)malloc(sizeof(float) * n);
    d->l_im     = (float *)malloc(sizeof(float) * n);
    d->r_re     = (float *)malloc(sizeof(float) * n);
    d->r_im     = (float *)malloc(sizeof(float) * n);
    d->mag_prev = (float *)calloc(d->bins, sizeof(float));
    d->mag_curr = (float *)calloc(d->bins, sizeof(float));
    if (!d->window || !d->l_re || !d->l_im || !d->r_re || !d->r_im
        || !d->mag_prev || !d->mag_curr) {
        detector_free(d);
        return -1;
    }

    for (size_t i = 0; i < n; ++i)
        d->window[i] = 0.5f * (1.0f - cosf((float)(2.0 * M_PI * (double)i / (double)(n - 1))));

    d->nf_foot = d->nf_gun = d->nf_gun_ultra = 1e-8f;
    d->nf_veh  = d->nf_expl = 1e-8f;
    d->nf_flux_gun = d->nf_flux_foot = d->nf_flux_expl = 1e-6f;
    /* Route through the setter so init and the live slider apply the exact
     * same [0.1, 5.0] clamp; a persisted out-of-range value would otherwise
     * survive init but be unreachable afterwards. */
    detector_set_sensitivity(d, sensitivity);

    apply_profile_params(d, profile ? profile : profile_by_index(0));

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    d->qpc_freq = (double)f.QuadPart;
    return 0;
}

void detector_free(SoundDetector *d) {
    if (!d) return;
    fft_free(&d->fft);
    free(d->window);   d->window   = NULL;
    free(d->l_re);     d->l_re     = NULL;
    free(d->l_im);     d->l_im     = NULL;
    free(d->r_re);     d->r_re     = NULL;
    free(d->r_im);     d->r_im     = NULL;
    free(d->mag_prev); d->mag_prev = NULL;
    free(d->mag_curr); d->mag_curr = NULL;
}

void detector_set_sensitivity(SoundDetector *d, float s) {
    if (!d) return;
    if (s < 0.1f) s = 0.1f;
    if (s > 5.0f) s = 5.0f;
    d->sensitivity = s;
}

void detector_apply_profile(SoundDetector *d, const GameProfile *p) {
    if (!d || !p) return;
    apply_profile_params(d, p);
    d->nf_foot = d->nf_gun = d->nf_gun_ultra = 1e-8f;
    d->nf_veh  = d->nf_expl = 1e-8f;
    d->nf_flux_gun = d->nf_flux_foot = d->nf_flux_expl = 1e-6f;
    d->frame_count = 0;
}

void detector_set_enable(SoundDetector *d, SoundEventKind kind, int on) {
    if (!d) return;
    switch (kind) {
        case SE_FOOTSTEP:  d->enable_foot = on; break;
        case SE_GUNSHOT:   d->enable_gun  = on; break;
        case SE_VEHICLE:   d->enable_veh  = on; break;
        case SE_EXPLOSION: d->enable_expl = on; break;
    }
}

/* Map a frequency range to a half-open bin range [k0, k1).
 *
 * k0 is the first bin at or above f0 (ceil) and k1 is one past the bin
 * containing f1 (floor + 1), so the band is covered without bleeding in a
 * bin below f0 or dropping the bin at the top edge. Truncating both edges
 * instead would shift every band down by up to one bin — e.g. the MW2
 * 60-180 Hz footstep band would actually read 47-164 Hz, letting exactly
 * the sub-50 Hz LFE content the profile is designed to exclude inflate the
 * footstep noise floor.
 *
 * Bin 0 (DC) is always excluded: it carries the signal mean, not audio
 * content, and would otherwise be half of a narrow sub-bass vehicle band.
 * Returns 0 if the resulting range is empty. */
static int band_bins(size_t bins, float bin_hz, float f0, float f1,
                     size_t *out_k0, size_t *out_k1) {
    if (f0 >= f1 || f1 <= 0.0f || bin_hz <= 0.0f) return 0;
    size_t k0 = (size_t)ceilf(f0 / bin_hz);
    size_t k1 = (size_t)floorf(f1 / bin_hz) + 1;
    if (k0 < 1)    k0 = 1;
    if (k1 > bins) k1 = bins;
    if (k0 >= k1) return 0;
    *out_k0 = k0;
    *out_k1 = k1;
    return 1;
}

static float band_power(const float *mag, size_t bins,
                        float bin_hz, float f0, float f1) {
    size_t k0, k1;
    if (!band_bins(bins, bin_hz, f0, f1, &k0, &k1)) return 0.0f;
    double s = 0.0;
    for (size_t k = k0; k < k1; ++k) s += (double)mag[k] * (double)mag[k];
    return (float)(s / (double)(k1 - k0));
}

static float band_flux(const float *prev, const float *curr, size_t bins,
                       float bin_hz, float f0, float f1) {
    size_t k0, k1;
    if (!band_bins(bins, bin_hz, f0, f1, &k0, &k1)) return 0.0f;
    double s = 0.0;
    for (size_t k = k0; k < k1; ++k) {
        float d = curr[k] - prev[k];
        if (d > 0) s += d;
    }
    return (float)(s / (double)(k1 - k0));
}

static float clamp_strength(float v) {
    if (v < 1.0f) return 1.0f;
    if (v > 3.0f) return 3.0f;
    return v;
}

/* Stereo pan computed from L/R energy *within a single frequency band*.
 * This localizes each event using only the energy in its own band, so a
 * footstep on the left is not pulled toward center by music or gunfire
 * that happens to be centered. Returns -1 (left) .. +1 (right). */
static float band_pan(const float *Lre, const float *Lim,
                      const float *Rre, const float *Rim,
                      size_t bins, float bin_hz, float f0, float f1) {
    size_t k0, k1;
    if (!band_bins(bins, bin_hz, f0, f1, &k0, &k1)) return 0.0f;
    double el = 0.0, er = 0.0;
    for (size_t k = k0; k < k1; ++k) {
        el += (double)Lre[k] * Lre[k] + (double)Lim[k] * Lim[k];
        er += (double)Rre[k] * Rre[k] + (double)Rim[k] * Rim[k];
    }
    double rl = sqrt(el) + 1e-9;
    double rr = sqrt(er) + 1e-9;
    float pan = (float)((rr - rl) / (rr + rl));
    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;
    return pan;
}

/* Map clamped strength (1..3) to a proximity radius hint: louder events
 * are treated as closer and drawn nearer the HUD center. */
static float strength_to_distance(float strength) {
    float d = 1.0f - (strength - 1.0f) / 2.0f;  /* 1..3 -> 1..0 */
    if (d < 0.12f) d = 0.12f;                     /* never dead-center */
    if (d > 1.0f)  d = 1.0f;
    return d;
}

int detector_analyze(SoundDetector *d,
                     const float *left,
                     const float *right,
                     SoundEvent *events,
                     int max_events) {
    if (!d || !left || !right || !events || max_events <= 0) return 0;

    size_t n = AUDIO_FFT_FRAMES;

    /* Transform the left and right channels separately. By linearity the
     * mono spectrum equals 0.5*(L + R) per bin, so we recover the exact
     * mono magnitude used for detection AND retain per-channel spectra for
     * accurate per-band stereo localization. */
    for (size_t i = 0; i < n; ++i) {
        d->l_re[i] = left[i]  * d->window[i];
        d->l_im[i] = 0.0f;
        d->r_re[i] = right[i] * d->window[i];
        d->r_im[i] = 0.0f;
    }
    fft_forward(&d->fft, d->l_re, d->l_im);
    fft_forward(&d->fft, d->r_re, d->r_im);

    float *prev = d->mag_prev;
    float *curr = d->mag_curr;
    for (size_t k = 0; k < d->bins; ++k) {
        float mre = 0.5f * (d->l_re[k] + d->r_re[k]);
        float mim = 0.5f * (d->l_im[k] + d->r_im[k]);
        curr[k] = sqrtf(mre * mre + mim * mim);
    }

    float bin_hz = (float)AUDIO_SAMPLE_RATE / (float)n;

    float p_foot  = band_power(curr, d->bins, bin_hz, d->foot_lo, d->foot_hi);
    float p_gun   = band_power(curr, d->bins, bin_hz, d->gun_lo,  d->gun_hi);
    float p_gun_u = band_power(curr, d->bins, bin_hz, d->gun_ultra_lo, d->gun_ultra_hi);
    float p_veh   = band_power(curr, d->bins, bin_hz, d->veh_lo,  d->veh_hi);
    float p_expl  = band_power(curr, d->bins, bin_hz, d->expl_lo, d->expl_hi);

    float flux_gun  = band_flux(prev, curr, d->bins, bin_hz, d->gun_lo, d->gun_ultra_hi);
    float flux_foot = band_flux(prev, curr, d->bins, bin_hz, d->foot_lo, d->foot_hi);
    float flux_expl = band_flux(prev, curr, d->bins, bin_hz, d->expl_lo, d->expl_hi);

    /* Compute every detection ratio against the noise floors as they stood
     * *before* this frame, then update the floors. Folding the current frame
     * in first would make each event inflate its own reference: the ratio
     * would collapse to p/((1-a)*nf_old + a*p), which is hard-capped at 1/a
     * (only 40x for nf_alpha=0.025) no matter how loud the transient is —
     * putting the louder profile thresholds within ~2x of an unreachable
     * ceiling and silently killing detection at high sensitivity. */
    float r_foot  = p_foot  / (d->nf_foot      + 1e-12f);
    float r_gun   = p_gun   / (d->nf_gun       + 1e-12f);
    float r_gun_u = p_gun_u / (d->nf_gun_ultra + 1e-12f);
    float r_veh   = p_veh   / (d->nf_veh       + 1e-12f);
    float r_expl  = p_expl  / (d->nf_expl      + 1e-12f);
    float r_fluxG = flux_gun  / (d->nf_flux_gun  + 1e-12f);
    float r_fluxF = flux_foot / (d->nf_flux_foot + 1e-12f);
    float r_fluxE = flux_expl / (d->nf_flux_expl + 1e-12f);

    float a = d->nf_alpha;
    d->nf_foot      = (1.0f - a) * d->nf_foot      + a * p_foot;
    d->nf_gun       = (1.0f - a) * d->nf_gun       + a * p_gun;
    d->nf_gun_ultra = (1.0f - a) * d->nf_gun_ultra + a * p_gun_u;
    d->nf_veh       = (1.0f - a) * d->nf_veh       + a * p_veh;
    d->nf_expl      = (1.0f - a) * d->nf_expl      + a * p_expl;
    d->nf_flux_gun  = (1.0f - a) * d->nf_flux_gun  + a * flux_gun;
    d->nf_flux_foot = (1.0f - a) * d->nf_flux_foot + a * flux_foot;
    d->nf_flux_expl = (1.0f - a) * d->nf_flux_expl + a * flux_expl;

    /* Warmup guard. */
    d->frame_count++;
    if (d->frame_count < (unsigned)d->warmup) {
        float *tw = d->mag_prev; d->mag_prev = d->mag_curr; d->mag_curr = tw;
        return 0;
    }

    int emitted = 0;
    double now = now_seconds(d);
    float s = d->sensitivity;

    /* Gunshot */
    if (d->enable_gun && emitted < max_events
        && r_fluxG > d->gun_flux_thresh * s
        && r_gun   > d->gun_power_thresh * s
        && r_gun_u > d->gun_ultra_thresh * s
        && (p_gun + p_gun_u) > 5e-5f
        && (now - d->last_gun_s) > d->gun_cooldown) {
        SoundEvent *e = &events[emitted++];
        e->kind = SE_GUNSHOT;
        e->pan = band_pan(d->l_re, d->l_im, d->r_re, d->r_im,
                          d->bins, bin_hz, d->gun_lo, d->gun_ultra_hi);
        e->strength = clamp_strength(
            (r_fluxG / (d->gun_flux_thresh * s)) * 0.5f +
            (r_gun   / (d->gun_power_thresh * s)) * 0.5f);
        e->distance = strength_to_distance(e->strength);
        e->timestamp = now;
        d->last_gun_s = now;
    }

    /* Explosion */
    if (d->enable_expl && emitted < max_events
        && r_expl > d->expl_thresh * s
        && r_fluxE > s
        && p_expl > 5e-4f
        && (now - d->last_expl_s) > d->expl_cooldown) {
        SoundEvent *e = &events[emitted++];
        e->kind = SE_EXPLOSION;
        e->pan = band_pan(d->l_re, d->l_im, d->r_re, d->r_im,
                          d->bins, bin_hz, d->expl_lo, d->expl_hi);
        e->strength = clamp_strength(r_expl / (d->expl_thresh * s));
        e->distance = strength_to_distance(e->strength);
        e->timestamp = now;
        d->last_expl_s = now;
    }

    /* Footstep */
    if (d->enable_foot && emitted < max_events
        && r_foot > d->foot_thresh * s
        && r_fluxF > s
        && p_foot > 4e-5f
        && p_foot > 1.4f * p_gun
        && (now - d->last_foot_s) > d->foot_cooldown) {
        SoundEvent *e = &events[emitted++];
        e->kind = SE_FOOTSTEP;
        e->pan = band_pan(d->l_re, d->l_im, d->r_re, d->r_im,
                          d->bins, bin_hz, d->foot_lo, d->foot_hi);
        e->strength = clamp_strength(r_foot / (d->foot_thresh * s));
        e->distance = strength_to_distance(e->strength);
        e->timestamp = now;
        d->last_foot_s = now;
    }

    /* Vehicle */
    if (d->enable_veh && emitted < max_events
        && r_veh > d->veh_thresh * s
        && p_veh > 1e-3f
        && (now - d->last_veh_s) > d->veh_cooldown) {
        SoundEvent *e = &events[emitted++];
        e->kind = SE_VEHICLE;
        e->pan = band_pan(d->l_re, d->l_im, d->r_re, d->r_im,
                          d->bins, bin_hz, d->veh_lo, d->veh_hi);
        e->strength = clamp_strength(r_veh / (d->veh_thresh * s));
        e->distance = strength_to_distance(e->strength);
        e->timestamp = now;
        d->last_veh_s = now;
    }

    float *tw = d->mag_prev; d->mag_prev = d->mag_curr; d->mag_curr = tw;
    return emitted;
}
