/* WASAPI loopback capture.
 *
 * Spawns a worker thread that opens the currently selected render endpoint
 * in loopback mode and delivers stereo float frames into a lock-free-ish
 * ring buffer. The detector thread consumes blocks of AUDIO_BLOCK_FRAMES
 * samples (hop); the ring is always 50% overlapped for FFT analysis.
 */
#ifndef SOUND_OVERLAY_AUDIO_H
#define SOUND_OVERLAY_AUDIO_H

#include <windows.h>
#include <stddef.h>

#define AUDIO_SAMPLE_RATE   48000u
#define AUDIO_BLOCK_FRAMES  1024u   /* FFT hop size (stereo frames) */
#define AUDIO_FFT_FRAMES    2048u   /* FFT window size (stereo frames) */
#define AUDIO_MAX_DEVICES   32

typedef struct {
    wchar_t id[256];
    wchar_t name[256];
    int     is_default;
} AudioDeviceInfo;

typedef struct AudioCapture AudioCapture;

/* Enumerate active render endpoints. Returns the number written to `out`
 * (bounded by max_out). The first entry is always the system default. */
int audio_list_devices(AudioDeviceInfo *out, int max_out);

/* Create a capture object. device_id may be NULL for the default device. */
AudioCapture *audio_capture_create(const wchar_t *device_id);
void          audio_capture_destroy(AudioCapture *c);

/* Start/stop the worker thread. Returns 0 on success. */
int  audio_capture_start(AudioCapture *c);
void audio_capture_stop(AudioCapture *c);

/* Pull the latest FFT-sized analysis window. Returns:
 *   1 on success (window written to out_fft_left/out_fft_right),
 *   0 on timeout,
 *  -1 if the capture thread has died.
 *
 * out_fft_left / out_fft_right must EACH have AUDIO_FFT_FRAMES floats.
 * On success they receive the most recent AUDIO_FFT_FRAMES samples and the
 * reader advances by one AUDIO_BLOCK_FRAMES hop, giving 50% overlap.
 */
int audio_capture_next_window(AudioCapture *c,
                              DWORD timeout_ms,
                              float *out_fft_left,
                              float *out_fft_right);

/* Check whether the capture thread is healthy. */
int  audio_capture_is_running(const AudioCapture *c);
HRESULT audio_capture_last_error(const AudioCapture *c);

#endif
