"""
audio_capture.py – Windows WASAPI loopback audio capture.

Captures whatever is playing through the system speakers so the overlay
can react to in-game sounds without needing access to the game process.
"""

import queue
import threading
import time

import numpy as np
import sounddevice as sd

import config


def find_loopback_device() -> int | None:
    """
    Search for a WASAPI loopback (system-audio capture) device.

    Priority order:
      1. Devices whose name contains well-known loopback keywords.
      2. Any WASAPI device that reports input channels on an output device.
      3. None → let sounddevice use the default input (e.g. microphone fallback).
    """
    keywords = ("stereo mix", "what u hear", "wave out mix",
                 "loopback", "mix output", "speaker")

    devices   = sd.query_devices()
    hostapis  = sd.query_hostapis()

    wasapi_index = next(
        (i for i, h in enumerate(hostapis) if "wasapi" in h["name"].lower()),
        None,
    )

    best_fallback = None

    for idx, dev in enumerate(devices):
        name = dev["name"].lower()

        # Keyword match – highest priority
        if any(k in name for k in keywords) and dev["max_input_channels"] > 0:
            return idx

        # WASAPI device that can do input (potential loopback)
        if wasapi_index is not None and dev.get("hostapi") == wasapi_index:
            if dev["max_input_channels"] > 0 and best_fallback is None:
                best_fallback = idx

    return best_fallback  # may be None


class AudioCapture:
    """
    Captures system audio in a background thread and feeds numpy chunks
    into *out_queue* for the analyser to consume.
    """

    def __init__(self, out_queue: queue.Queue):
        self._q       = out_queue
        self._thread  = None
        self._running = False
        self._stream  = None
        self.device_name = "unknown"

    # ── public API ─────────────────────────────────────────────────────────────

    def start(self) -> str:
        """Start capture; returns the device name being used."""
        self._running = True
        self._thread  = threading.Thread(target=self._run, daemon=True, name="AudioCapture")
        self._thread.start()
        time.sleep(0.4)                   # give the thread time to open the stream
        return self.device_name

    def stop(self):
        self._running = False
        if self._stream:
            try:
                self._stream.stop()
                self._stream.close()
            except Exception:
                pass
        if self._thread:
            self._thread.join(timeout=2)

    # ── internals ──────────────────────────────────────────────────────────────

    def _callback(self, indata: np.ndarray, frames: int, time_info, status):
        """sounddevice callback – runs on the audio thread."""
        if self._running:
            self._q.put(indata.copy())

    def _run(self):
        device_id = find_loopback_device()

        # Build kwargs; if no loopback found fall back to default input
        stream_kwargs = dict(
            samplerate = config.SAMPLE_RATE,
            channels   = config.CHANNELS,
            blocksize  = config.CHUNK_SIZE,
            dtype      = "float32",
            callback   = self._callback,
            latency    = "low",
        )
        if device_id is not None:
            stream_kwargs["device"] = device_id

        try:
            dev_info = sd.query_devices(device_id) if device_id is not None else sd.query_devices(sd.default.device[0])
            self.device_name = dev_info["name"]
        except Exception:
            self.device_name = "default"

        while self._running:
            try:
                with sd.InputStream(**stream_kwargs) as stream:
                    self._stream = stream
                    while self._running:
                        time.sleep(0.05)
            except sd.PortAudioError as exc:
                # WASAPI loopback requires exclusive-mode flag on some drivers;
                # retry with the default device instead.
                print(f"[audio] {exc} — retrying with default input device…")
                stream_kwargs.pop("device", None)
                self.device_name = "default (mic fallback)"
                time.sleep(1)
            except Exception as exc:
                print(f"[audio] Unexpected error: {exc}")
                time.sleep(2)
