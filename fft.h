/* Minimal radix-2 Cooley-Tukey FFT, in-place, interleaved complex input. */
#ifndef SOUND_OVERLAY_FFT_H
#define SOUND_OVERLAY_FFT_H

#include <stddef.h>

typedef struct {
    size_t    n;            /* power of 2 */
    unsigned  log2n;
    float    *twiddle_re;   /* length n/2 */
    float    *twiddle_im;   /* length n/2 */
    unsigned *bitrev;       /* length n */
} FFT;

/* Returns 0 on success, non-zero on error. n must be a power of 2 >= 2. */
int  fft_init(FFT *f, size_t n);
void fft_free(FFT *f);

/* Forward FFT. re[] and im[] each have length n; result replaces input. */
void fft_forward(const FFT *f, float *re, float *im);

/* Convenience: magnitude spectrum of a real signal of length n.
 *   in  : n real samples (already windowed by the caller)
 *   mag : length n/2 + 1 output, |X[k]|
 * scratch_re / scratch_im must each have length n.
 */
void fft_magnitude_real(const FFT *f,
                        const float *in,
                        float       *scratch_re,
                        float       *scratch_im,
                        float       *mag);

#endif
