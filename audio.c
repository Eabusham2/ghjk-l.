#include "audio.h"

#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <avrt.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

/* ---- ring buffer for stereo float frames ---------------------------- */

typedef struct {
    float           *left;
    float           *right;
    size_t           cap;      /* frames */
    volatile size_t  wr;
    volatile size_t  rd;
    CRITICAL_SECTION lock;
    HANDLE           data_event;
} FrameRing;

static void ring_init(FrameRing *r, size_t cap) {
    r->cap = cap;
    r->left  = (float *)calloc(cap, sizeof(float));
    r->right = (float *)calloc(cap, sizeof(float));
    r->wr = r->rd = 0;
    InitializeCriticalSection(&r->lock);
    r->data_event = CreateEventW(NULL, FALSE, FALSE, NULL);
}

static void ring_free(FrameRing *r) {
    free(r->left); free(r->right);
    DeleteCriticalSection(&r->lock);
    if (r->data_event) CloseHandle(r->data_event);
    memset(r, 0, sizeof(*r));
}

static size_t ring_available(const FrameRing *r) {
    /* writer - reader, wrap-aware via modular difference */
    size_t wr = r->wr, rd = r->rd;
    return (wr - rd);
}

static void ring_push(FrameRing *r, const float *l, const float *rr, size_t n) {
    EnterCriticalSection(&r->lock);
    for (size_t i = 0; i < n; ++i) {
        size_t idx = (r->wr + i) % r->cap;
        r->left [idx] = l[i];
        r->right[idx] = rr[i];
    }
    r->wr += n;
    /* Drop old data if ring overflowed so reader catches up. */
    size_t avail = r->wr - r->rd;
    if (avail > r->cap) {
        r->rd = r->wr - r->cap;
    }
    LeaveCriticalSection(&r->lock);
    SetEvent(r->data_event);
}

/* Read `n` frames starting `offset_behind_writer` frames before the writer.
 * offset=0 means "the latest n frames". Returns 1 on success, 0 if not enough. */
static int ring_peek_latest(FrameRing *r, float *out_l, float *out_r, size_t n) {
    int ok = 0;
    EnterCriticalSection(&r->lock);
    size_t avail = r->wr - r->rd;
    if (avail >= n) {
        size_t start = r->wr - n;
        for (size_t i = 0; i < n; ++i) {
            size_t idx = (start + i) % r->cap;
            out_l[i] = r->left [idx];
            out_r[i] = r->right[idx];
        }
        ok = 1;
    }
    LeaveCriticalSection(&r->lock);
    return ok;
}

/* Advance the reader by n frames. */
static void ring_consume(FrameRing *r, size_t n) {
    EnterCriticalSection(&r->lock);
    size_t avail = r->wr - r->rd;
    if (n > avail) n = avail;
    r->rd += n;
    LeaveCriticalSection(&r->lock);
}

/* ---- capture object -------------------------------------------------- */

struct AudioCapture {
    wchar_t          device_id[256];
    HANDLE           thread;
    volatile LONG    running;
    volatile LONG    stop_request;
    HRESULT          last_error;
    FrameRing        ring;
};

/* ---- device enumeration --------------------------------------------- */

int audio_list_devices(AudioDeviceInfo *out, int max_out) {
    if (max_out <= 0) return 0;
    int written = 0;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int did_init = SUCCEEDED(hr);

    IMMDeviceEnumerator *en = NULL;
    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL,
                                CLSCTX_ALL, &IID_IMMDeviceEnumerator,
                                (void **)&en))) {
        goto done;
    }

    IMMDevice *def = NULL;
    LPWSTR    def_id = NULL;
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(
                        en, eRender, eConsole, &def))) {
        IMMDevice_GetId(def, &def_id);
    }

    IMMDeviceCollection *col = NULL;
    if (SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(
                        en, eRender, DEVICE_STATE_ACTIVE, &col))) {
        UINT count = 0;
        IMMDeviceCollection_GetCount(col, &count);
        for (UINT i = 0; i < count && written < max_out; ++i) {
            IMMDevice *d = NULL;
            if (FAILED(IMMDeviceCollection_Item(col, i, &d))) continue;

            LPWSTR  id = NULL;
            IMMDevice_GetId(d, &id);

            IPropertyStore *props = NULL;
            wchar_t name[256] = L"(unknown)";
            if (SUCCEEDED(IMMDevice_OpenPropertyStore(d, STGM_READ, &props))) {
                PROPVARIANT pv;
                PropVariantInit(&pv);
                if (SUCCEEDED(IPropertyStore_GetValue(props, &PKEY_Device_FriendlyName, &pv))
                    && pv.vt == VT_LPWSTR && pv.pwszVal) {
                    lstrcpynW(name, pv.pwszVal, 256);
                }
                PropVariantClear(&pv);
                IPropertyStore_Release(props);
            }

            AudioDeviceInfo *info = &out[written++];
            memset(info, 0, sizeof(*info));
            if (id) lstrcpynW(info->id, id, 256);
            lstrcpynW(info->name, name, 256);
            info->is_default = (def_id && id && lstrcmpW(def_id, id) == 0) ? 1 : 0;

            if (id) CoTaskMemFree(id);
            IMMDevice_Release(d);
        }
        IMMDeviceCollection_Release(col);
    }

    if (def_id) CoTaskMemFree(def_id);
    if (def)    IMMDevice_Release(def);
    IMMDeviceEnumerator_Release(en);

    /* Ensure default appears first. */
    for (int i = 1; i < written; ++i) {
        if (out[i].is_default) {
            AudioDeviceInfo tmp = out[0];
            out[0] = out[i];
            out[i] = tmp;
            break;
        }
    }

done:
    if (did_init) CoUninitialize();
    return written;
}

/* ---- capture thread -------------------------------------------------- */

typedef struct {
    int   is_float;     /* IEEE float vs. int */
    int   bits;
    int   bytes_per_sample;
    int   channels;
    int   samplerate;
} MixFormat;

static int parse_mix_format(const WAVEFORMATEX *wfx, MixFormat *mf) {
    mf->channels = wfx->nChannels;
    mf->samplerate = wfx->nSamplesPerSec;
    mf->bits = wfx->wBitsPerSample;
    mf->bytes_per_sample = wfx->wBitsPerSample / 8;

    if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        mf->is_float = 1;
        return 1;
    }
    if (wfx->wFormatTag == WAVE_FORMAT_PCM) {
        mf->is_float = 0;
        return 1;
    }
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)wfx;
        /* KSDATAFORMAT_SUBTYPE_PCM and _IEEE_FLOAT share the fixed suffix
         * {0000-0010-8000-00aa00389b71}; the leading Data1 field holds the
         * WAVE_FORMAT_* tag. Comparing Data1 avoids depending on the GUID
         * symbols being instantiated by a particular toolchain/import lib. */
        if (ext->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT) {
            mf->is_float = 1;
            return 1;
        }
        if (ext->SubFormat.Data1 == WAVE_FORMAT_PCM) {
            mf->is_float = 0;
            return 1;
        }
    }
    return 0;
}

/* Downmix arbitrary channel layout to stereo floats in [-1, 1].
 *
 *   1ch:   mono -> both
 *   2ch:   passthrough
 *   3/4ch: L/R/C/(LFE or back)
 *   6ch:   5.1  FL FR FC LFE BL BR
 *   8ch:   7.1  FL FR FC LFE BL BR SL SR
 *  default: first two channels.
 */
static void downmix_to_stereo(const void *src, int is_float, int bits,
                              int channels, UINT32 frames,
                              float *dst_l, float *dst_r) {
    const unsigned char *bytes = (const unsigned char *)src;
    int bps = bits / 8;
    size_t stride = (size_t)channels * bps;

    for (UINT32 i = 0; i < frames; ++i) {
        float ch[8] = {0};
        int n = channels < 8 ? channels : 8;
        for (int c = 0; c < n; ++c) {
            const unsigned char *p = bytes + i * stride + c * bps;
            float v = 0.0f;
            if (is_float) {
                v = *(const float *)p;
            } else {
                if (bits == 16) {
                    int16_t s = *(const int16_t *)p;
                    v = (float)s / 32768.0f;
                } else if (bits == 24) {
                    int32_t s = (int32_t)p[0] | ((int32_t)p[1] << 8) | ((int32_t)(int8_t)p[2] << 16);
                    v = (float)s / 8388608.0f;
                } else if (bits == 32) {
                    int32_t s = *(const int32_t *)p;
                    v = (float)s / 2147483648.0f;
                }
            }
            ch[c] = v;
        }

        float l, r;
        if (channels == 1) {
            l = r = ch[0];
        } else if (channels == 2) {
            l = ch[0]; r = ch[1];
        } else if (channels == 6) {
            /* 5.1 -> stereo using ITU 775-style weights, ignoring LFE. */
            l = ch[0] + 0.707f * ch[2] + 0.707f * ch[4];
            r = ch[1] + 0.707f * ch[2] + 0.707f * ch[5];
        } else if (channels == 8) {
            l = ch[0] + 0.707f * ch[2] + 0.707f * ch[4] + 0.707f * ch[6];
            r = ch[1] + 0.707f * ch[2] + 0.707f * ch[5] + 0.707f * ch[7];
        } else {
            l = ch[0];
            r = channels > 1 ? ch[1] : ch[0];
        }
        /* Light soft clip. */
        if (l >  1.5f) l =  1.5f; if (l < -1.5f) l = -1.5f;
        if (r >  1.5f) r =  1.5f; if (r < -1.5f) r = -1.5f;
        dst_l[i] = l;
        dst_r[i] = r;
    }
}

static DWORD WINAPI audio_thread(LPVOID arg) {
    AudioCapture *c = (AudioCapture *)arg;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int co_init = SUCCEEDED(hr);

    IMMDeviceEnumerator *en = NULL;
    IMMDevice           *dev = NULL;
    IAudioClient        *ac  = NULL;
    IAudioCaptureClient *cc  = NULL;
    WAVEFORMATEX        *wfx = NULL;
    HANDLE               mmcss = NULL;
    float               *mix_l = NULL;
    float               *mix_r = NULL;
    size_t               mix_cap = 0;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void **)&en);
    if (FAILED(hr)) goto fail;

    if (c->device_id[0]) {
        hr = IMMDeviceEnumerator_GetDevice(en, c->device_id, &dev);
    } else {
        hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev);
    }
    if (FAILED(hr)) goto fail;

    hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&ac);
    if (FAILED(hr)) goto fail;

    hr = IAudioClient_GetMixFormat(ac, &wfx);
    if (FAILED(hr)) goto fail;

    MixFormat mf;
    if (!parse_mix_format(wfx, &mf)) {
        hr = E_FAIL;
        goto fail;
    }

    REFERENCE_TIME buf_duration = 10000000; /* 1 second */
    hr = IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK,
                                 buf_duration, 0, wfx, NULL);
    if (FAILED(hr)) goto fail;

    hr = IAudioClient_GetService(ac, &IID_IAudioCaptureClient, (void **)&cc);
    if (FAILED(hr)) goto fail;

    DWORD mmcss_idx = 0;
    mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcss_idx);

    hr = IAudioClient_Start(ac);
    if (FAILED(hr)) goto fail;

    InterlockedExchange(&c->running, 1);

    /* Resample-on-capture is out of scope; if the endpoint rate differs,
     * we still consume frames at the mix rate and store them - the
     * detector bin frequencies will be scaled accordingly via the
     * reported samplerate. We expose mf.samplerate to the caller via
     * the ring but keep naming simple: we still push samples and the
     * detector assumes the declared AUDIO_SAMPLE_RATE. So we nearest-
     * neighbour resample here to 48 kHz when needed to keep the rest
     * of the pipeline simple. */
    const UINT32 target_sr = AUDIO_SAMPLE_RATE;
    double resample_ratio = (double)target_sr / (double)mf.samplerate;
    double frac_accum = 0.0;

    while (!InterlockedCompareExchange(&c->stop_request, 0, 0)) {
        UINT32 packet = 0;
        hr = IAudioCaptureClient_GetNextPacketSize(cc, &packet);
        if (FAILED(hr)) break;

        if (packet == 0) {
            Sleep(3);
            continue;
        }

        BYTE    *data = NULL;
        UINT32   frames = 0;
        DWORD    flags = 0;
        hr = IAudioCaptureClient_GetBuffer(cc, &data, &frames, &flags, NULL, NULL);
        if (FAILED(hr)) break;

        if (mix_cap < frames) {
            free(mix_l); free(mix_r);
            mix_cap = frames * 2 + 256;
            mix_l = (float *)malloc(sizeof(float) * mix_cap);
            mix_r = (float *)malloc(sizeof(float) * mix_cap);
            if (!mix_l || !mix_r) { IAudioCaptureClient_ReleaseBuffer(cc, frames); hr = E_OUTOFMEMORY; break; }
        }

        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            memset(mix_l, 0, sizeof(float) * frames);
            memset(mix_r, 0, sizeof(float) * frames);
        } else {
            downmix_to_stereo(data, mf.is_float, mf.bits, mf.channels,
                              frames, mix_l, mix_r);
        }

        IAudioCaptureClient_ReleaseBuffer(cc, frames);

        /* Simple linear resample to AUDIO_SAMPLE_RATE when mix rate differs. */
        if (mf.samplerate == (int)target_sr) {
            ring_push(&c->ring, mix_l, mix_r, frames);
        } else {
            /* Compute output frames: floor(frames*ratio) with fractional carryover. */
            double want_d = frames * resample_ratio + frac_accum;
            size_t want = (size_t)want_d;
            frac_accum = want_d - (double)want;

            /* Reuse end of mix arrays if they fit, else malloc a scratch. */
            static float *res_l = NULL, *res_r = NULL;
            static size_t res_cap = 0;
            if (res_cap < want) {
                free(res_l); free(res_r);
                res_cap = want * 2 + 256;
                res_l = (float *)malloc(sizeof(float) * res_cap);
                res_r = (float *)malloc(sizeof(float) * res_cap);
            }
            if (res_l && res_r) {
                double step = 1.0 / resample_ratio;
                for (size_t i = 0; i < want; ++i) {
                    double src_pos = (double)i * step;
                    size_t i0 = (size_t)src_pos;
                    if (i0 >= frames) i0 = frames - 1;
                    size_t i1 = i0 + 1 < frames ? i0 + 1 : i0;
                    float t = (float)(src_pos - (double)i0);
                    res_l[i] = mix_l[i0] * (1.0f - t) + mix_l[i1] * t;
                    res_r[i] = mix_r[i0] * (1.0f - t) + mix_r[i1] * t;
                }
                ring_push(&c->ring, res_l, res_r, want);
            }
        }
    }

fail:
    if (ac) IAudioClient_Stop(ac);
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    if (cc)  IAudioCaptureClient_Release(cc);
    if (ac)  IAudioClient_Release(ac);
    if (wfx) CoTaskMemFree(wfx);
    if (dev) IMMDevice_Release(dev);
    if (en)  IMMDeviceEnumerator_Release(en);
    free(mix_l); free(mix_r);

    c->last_error = hr;
    InterlockedExchange(&c->running, 0);
    if (co_init) CoUninitialize();
    return 0;
}

/* ---- public API ------------------------------------------------------ */

AudioCapture *audio_capture_create(const wchar_t *device_id) {
    AudioCapture *c = (AudioCapture *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (device_id) lstrcpynW(c->device_id, device_id, 256);
    /* Ring big enough for ~1 second at 48 kHz. */
    ring_init(&c->ring, AUDIO_SAMPLE_RATE);
    return c;
}

void audio_capture_destroy(AudioCapture *c) {
    if (!c) return;
    audio_capture_stop(c);
    ring_free(&c->ring);
    free(c);
}

int audio_capture_start(AudioCapture *c) {
    if (!c) return -1;
    if (InterlockedCompareExchange(&c->running, 0, 0)) return 0;
    c->stop_request = 0;
    c->last_error = S_OK;
    c->thread = CreateThread(NULL, 0, audio_thread, c, 0, NULL);
    return c->thread ? 0 : -1;
}

void audio_capture_stop(AudioCapture *c) {
    if (!c) return;
    InterlockedExchange(&c->stop_request, 1);
    if (c->thread) {
        WaitForSingleObject(c->thread, 2000);
        CloseHandle(c->thread);
        c->thread = NULL;
    }
}

int audio_capture_is_running(const AudioCapture *c) {
    return c ? (int)c->running : 0;
}

HRESULT audio_capture_last_error(const AudioCapture *c) {
    return c ? c->last_error : E_FAIL;
}

int audio_capture_next_window(AudioCapture *c,
                              DWORD timeout_ms,
                              float *out_fft_left,
                              float *out_fft_right) {
    if (!c || !out_fft_left || !out_fft_right) return -1;

    DWORD start = GetTickCount();
    for (;;) {
        if (!c->running) return -1;
        EnterCriticalSection(&c->ring.lock);
        size_t avail = c->ring.wr - c->ring.rd;
        int enough = avail >= AUDIO_FFT_FRAMES;
        LeaveCriticalSection(&c->ring.lock);
        if (enough) break;
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= timeout_ms) return 0;
        WaitForSingleObject(c->ring.data_event,
                            timeout_ms - elapsed > 20 ? 20 : timeout_ms - elapsed);
    }

    /* Read the latest AUDIO_FFT_FRAMES samples (50% overlap window). */
    if (!ring_peek_latest(&c->ring, out_fft_left, out_fft_right, AUDIO_FFT_FRAMES)) {
        return 0;
    }
    /* Advance reader by one hop. */
    ring_consume(&c->ring, AUDIO_BLOCK_FRAMES);
    return 1;
}
