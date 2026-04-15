#include "fft.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int is_power_of_two(size_t n) {
    return n && ((n & (n - 1)) == 0);
}

int fft_init(FFT *f, size_t n) {
    if (!f || !is_power_of_two(n) || n < 2) return -1;
    memset(f, 0, sizeof(*f));
    f->n = n;
    f->log2n = 0;
    for (size_t t = n; t > 1; t >>= 1) f->log2n++;

    f->twiddle_re = (float *)malloc(sizeof(float) * (n / 2));
    f->twiddle_im = (float *)malloc(sizeof(float) * (n / 2));
    f->bitrev     = (unsigned *)malloc(sizeof(unsigned) * n);
    if (!f->twiddle_re || !f->twiddle_im || !f->bitrev) {
        fft_free(f);
        return -1;
    }

    for (size_t k = 0; k < n / 2; ++k) {
        double a = -2.0 * M_PI * (double)k / (double)n;
        f->twiddle_re[k] = (float)cos(a);
        f->twiddle_im[k] = (float)sin(a);
    }

    unsigned bits = f->log2n;
    for (unsigned i = 0; i < n; ++i) {
        unsigned r = 0, v = i;
        for (unsigned b = 0; b < bits; ++b) {
            r = (r << 1) | (v & 1u);
            v >>= 1;
        }
        f->bitrev[i] = r;
    }
    return 0;
}

void fft_free(FFT *f) {
    if (!f) return;
    free(f->twiddle_re); f->twiddle_re = NULL;
    free(f->twiddle_im); f->twiddle_im = NULL;
    free(f->bitrev);     f->bitrev     = NULL;
    f->n = 0;
    f->log2n = 0;
}

void fft_forward(const FFT *f, float *re, float *im) {
    size_t n = f->n;

    /* Bit-reversal permutation. */
    for (unsigned i = 0; i < n; ++i) {
        unsigned j = f->bitrev[i];
        if (j > i) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }

    /* Iterative butterflies. */
    for (unsigned s = 1; s <= f->log2n; ++s) {
        unsigned m    = 1u << s;       /* block size */
        unsigned half = m >> 1;
        unsigned step = (unsigned)(n / m);
        for (unsigned k = 0; k < n; k += m) {
            for (unsigned j = 0; j < half; ++j) {
                float wr = f->twiddle_re[j * step];
                float wi = f->twiddle_im[j * step];
                unsigned i0 = k + j;
                unsigned i1 = i0 + half;
                float xr = re[i1] * wr - im[i1] * wi;
                float xi = re[i1] * wi + im[i1] * wr;
                re[i1] = re[i0] - xr;
                im[i1] = im[i0] - xi;
                re[i0] = re[i0] + xr;
                im[i0] = im[i0] + xi;
            }
        }
    }
}

void fft_magnitude_real(const FFT *f,
                        const float *in,
                        float       *scratch_re,
                        float       *scratch_im,
                        float       *mag) {
    size_t n = f->n;
    memcpy(scratch_re, in, sizeof(float) * n);
    memset(scratch_im, 0, sizeof(float) * n);
    fft_forward(f, scratch_re, scratch_im);
    for (size_t k = 0; k <= n / 2; ++k) {
        float r = scratch_re[k];
        float i = scratch_im[k];
        mag[k] = sqrtf(r * r + i * i);
    }
}
