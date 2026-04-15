"""
sound_analyzer.py – Real-time frequency-domain sound event detection.

Reads raw stereo float32 chunks from *in_queue*, runs FFT analysis, and
emits SoundEvent objects into *out_queue* for the overlay to render.

Sound-type heuristics
──────────────────────
Gunshot:
  • Sharp amplitude spike (peak >> rolling average)
  • Broadband spectral content (significant energy above 1 kHz)
  • Per-event cooldown to suppress echo tails

Footstep:
  • Moderate amplitude bump
  • Energy concentrated in the 80–500 Hz band
  • Short cooldown to allow rhythmic detection without flooding

Vehicle / engine (bonus):
  • Sustained low-frequency content (< 200 Hz) above ambient

Direction:
  • Left / right from RMS balance between stereo channels → ±90°
  • Up / down not detectable from stereo alone; reserved for spatial audio
"""

import queue
import threading
import time
from collections import deque
from dataclasses import dataclass, field

import numpy as np

import config


# ── Data model ────────────────────────────────────────────────────────────────

@dataclass
class SoundEvent:
    kind:      str        # "gunshot" | "footstep" | "vehicle"
    angle_deg: float      # degrees; 0 = centre, -90 = hard left, +90 = hard right
    intensity: float      # 0.0–1.0
    timestamp: float = field(default_factory=time.time)


# ── Analyser ──────────────────────────────────────────────────────────────────

class SoundAnalyzer:
    """
    Consumes chunks from *in_queue* on a background thread and puts
    SoundEvent objects into *out_queue*.
    """

    def __init__(self, in_queue: queue.Queue, out_queue: queue.Queue):
        self._in   = in_queue
        self._out  = out_queue

        self._amp_history: deque[float] = deque(maxlen=config.AMPLITUDE_HISTORY_LEN)
        self._last_gunshot   = 0.0
        self._last_footstep  = 0.0
        self._last_vehicle   = 0.0

        self._thread  = None
        self._running = False

        # Pre-compute FFT frequency bins (updated lazily when chunk size changes)
        self._last_n   = 0
        self._freqs    = np.array([])

    # ── public API ─────────────────────────────────────────────────────────────

    def start(self):
        self._running = True
        self._thread  = threading.Thread(target=self._run, daemon=True, name="SoundAnalyzer")
        self._thread.start()

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=2)

    # ── internals ──────────────────────────────────────────────────────────────

    def _run(self):
        while self._running:
            try:
                chunk = self._in.get(timeout=0.2)
                self._process(chunk)
            except queue.Empty:
                continue
            except Exception as exc:
                print(f"[analyzer] Error: {exc}")

    def _process(self, chunk: np.ndarray):
        n_frames, n_ch = chunk.shape if chunk.ndim == 2 else (len(chunk), 1)

        left  = chunk[:, 0] if n_ch >= 1 else chunk.flatten()
        right = chunk[:, 1] if n_ch >= 2 else left
        mono  = (left + right) * 0.5

        # ── Amplitude ─────────────────────────────────────────────────────────
        peak = float(np.max(np.abs(chunk)))
        self._amp_history.append(peak)
        baseline = float(np.mean(self._amp_history)) if self._amp_history else 0.0

        # ── Stereo direction ──────────────────────────────────────────────────
        l_rms = float(np.sqrt(np.mean(left  ** 2)))
        r_rms = float(np.sqrt(np.mean(right ** 2)))
        total = l_rms + r_rms
        if total > 1e-8:
            # balance ∈ [-1, +1]; positive → sound from the right
            balance   = (r_rms - l_rms) / total
            angle_deg = balance * 90.0
        else:
            angle_deg = 0.0

        # ── FFT ───────────────────────────────────────────────────────────────
        n = len(mono)
        if n != self._last_n:
            self._freqs    = np.fft.rfftfreq(n, d=1.0 / config.SAMPLE_RATE)
            self._last_n   = n

        spectrum = np.abs(np.fft.rfft(mono))
        total_energy = float(np.sum(spectrum)) + 1e-12

        # Band energies
        high_mask = self._freqs > 1000
        low_mask  = (self._freqs >= 80) & (self._freqs <= 500)
        sub_mask  = (self._freqs >= 30) & (self._freqs <  200)

        high_ratio = float(np.sum(spectrum[high_mask])) / total_energy
        low_ratio  = float(np.sum(spectrum[low_mask]))  / total_energy
        sub_ratio  = float(np.sum(spectrum[sub_mask]))  / total_energy

        now = time.time()
        intensity = min(peak, 1.0)

        # ── Gunshot ───────────────────────────────────────────────────────────
        if (
            peak > config.GUNSHOT_AMPLITUDE_THRESH
            and peak > baseline * config.GUNSHOT_SPIKE_RATIO
            and high_ratio > config.GUNSHOT_HIGH_FREQ_RATIO
            and (now - self._last_gunshot) > config.GUNSHOT_COOLDOWN_SEC
        ):
            self._last_gunshot = now
            self._out.put(SoundEvent("gunshot", angle_deg, intensity))
            return                                  # don't double-classify

        # ── Footstep ──────────────────────────────────────────────────────────
        if (
            peak > config.FOOTSTEP_AMPLITUDE_THRESH
            and low_ratio > config.FOOTSTEP_LOW_FREQ_RATIO
            and (now - self._last_footstep) > config.FOOTSTEP_COOLDOWN_SEC
        ):
            self._last_footstep = now
            self._out.put(SoundEvent("footstep", angle_deg, intensity))
            return

        # ── Vehicle / engine (sustained low rumble) ───────────────────────────
        if (
            peak > config.FOOTSTEP_AMPLITUDE_THRESH * 0.8
            and sub_ratio > 0.55
            and (now - self._last_vehicle) > 0.5
        ):
            self._last_vehicle = now
            self._out.put(SoundEvent("vehicle", angle_deg, intensity))
