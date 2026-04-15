"""
main.py – Sound Detection Game Overlay entry point.

Architecture (3 threads + main):
  AudioCapture  → raw_q → SoundAnalyzer → event_q → Overlay (main thread)

Usage:
  python main.py [--pos bottom|top|left|right|center] [--size N] [--alpha 0.0-1.0]

Build EXE:
  build.bat   (runs PyInstaller with the right flags)
"""

import argparse
import queue
import signal
import sys

import config
from audio_capture import AudioCapture
from sound_analyzer import SoundAnalyzer
from overlay import Overlay


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Sound Detection Game Overlay — visualises in-game footsteps & gunshots"
    )
    p.add_argument("--pos",   default=config.OVERLAY_POSITION,
                   choices=["bottom", "top", "left", "right", "center"],
                   help="Screen position of the radar (default: %(default)s)")
    p.add_argument("--size",  type=int,   default=config.OVERLAY_SIZE,
                   help="Radar diameter in pixels (default: %(default)s)")
    p.add_argument("--alpha", type=float, default=config.OVERLAY_ALPHA,
                   help="Window opacity 0.0–1.0 (default: %(default)s)")
    p.add_argument("--fade",  type=float, default=config.EVENT_FADE_SEC,
                   help="Seconds an indicator stays visible (default: %(default)s)")
    return p.parse_args()


def apply_args(args: argparse.Namespace):
    config.OVERLAY_POSITION = args.pos
    config.OVERLAY_SIZE     = args.size
    config.OVERLAY_ALPHA    = max(0.05, min(1.0, args.alpha))
    config.EVENT_FADE_SEC   = max(0.5, args.fade)


def main():
    args = parse_args()
    apply_args(args)

    print("╔══════════════════════════════════════════╗")
    print("║    Sound Detection Game Overlay v1.0     ║")
    print("╠══════════════════════════════════════════╣")
    print("║  Captures system audio via WASAPI        ║")
    print("║  Detects: footsteps • gunshots • vehicles║")
    print("║  Controls: RMB-drag to reposition        ║")
    print("║            Escape / middle-click to quit ║")
    print("╚══════════════════════════════════════════╝")
    print()

    # ── Queues ──────────────────────────────────────────────────────────────
    raw_q   = queue.Queue(maxsize=64)   # raw audio chunks (AudioCapture → Analyzer)
    event_q = queue.Queue(maxsize=256)  # sound events     (Analyzer → Overlay)

    # ── Start subsystems ────────────────────────────────────────────────────
    capture  = AudioCapture(raw_q)
    analyzer = SoundAnalyzer(raw_q, event_q)
    overlay  = Overlay()

    device_name = capture.start()
    print(f"[audio]    Capturing from: {device_name}")

    analyzer.start()
    print("[analyzer] Sound analysis running.")
    print("[overlay]  Launching radar window…")
    print()

    # ── Graceful shutdown on Ctrl-C ──────────────────────────────────────────
    def _shutdown(*_):
        print("\n[main] Shutting down…")
        overlay.stop()
        analyzer.stop()
        capture.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT,  _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)

    # overlay.start() blocks on tkinter mainloop
    try:
        overlay.start(event_q)
    except KeyboardInterrupt:
        pass
    finally:
        analyzer.stop()
        capture.stop()
        print("[main] Goodbye.")


if __name__ == "__main__":
    main()
