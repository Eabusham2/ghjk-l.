# SoundOverlay

A native C / Win32 application that captures the audio your PC is already
playing (WASAPI loopback), runs FFT-based analysis on it, and displays a
transparent, click-through, always-on-top HUD that shows the direction of
**footsteps** and **gunshots** in games such as Fortnite, Apex, Warzone,
CS2, etc.

It is an accessibility / awareness tool. It does **not** read game memory,
inject into the game, or interact with the game — it only analyzes the
audio that the game has already sent to your speakers.

## Features

- **WASAPI loopback** capture of any active render endpoint (default
  speakers, a specific headset, etc.) without a virtual cable.
- **Accurate detection** pipeline:
  - 2048-sample Hann-windowed FFT at 48 kHz (1024-sample hop, 50% overlap).
  - Four-band power analysis (low 40-220 Hz, mid 300-1200 Hz,
    high 1.5-6 kHz, ultra 6-12 kHz).
  - **Spectral flux onset detection** in the high band to catch the
    broadband transient of a gunshot's crack + snap.
  - **Adaptive per-band noise floor** (exponential moving average) so
    the detector self-calibrates to each game's mix within a few
    seconds.
  - **Refractory periods** to avoid repeated triggers from the same
    event.
  - Stereo-pan direction from energy-weighted L/R ratio on the windowed
    signal.
- **GUI settings window** (standard Win32 controls) with:
  - Audio device combobox
  - Sensitivity slider (0.5x .. 2.0x threshold multiplier)
  - Overlay size slider (200 .. 600 px)
  - Overlay position (top-right / top-left / bottom-right / bottom-left /
    center)
  - Show-overlay checkbox
  - Start / Stop buttons
- **Transparent overlay** using `WS_EX_LAYERED | WS_EX_TRANSPARENT |
  WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE` with chroma-key
  transparency, drawn via double-buffered GDI at ~30 fps.
- **Global hotkey**: `Ctrl + F10` to quit.

No external dependencies beyond the Windows SDK — pure C, pure Win32.

## Source layout

```
fft.c/h        Radix-2 in-place FFT.
audio.c/h      WASAPI loopback capture + ring buffer + downmix + resample.
detector.c/h   FFT-based footstep / gunshot classifier.
overlay.c/h    Layered click-through HUD window (GDI).
main.c         Settings window, hotkey, thread orchestration (WinMain).
build.bat      MSVC build.
build-mingw.bat Alternative mingw-w64 build.
```

## Building

### With MSVC (recommended)

Open *x64 Native Tools Command Prompt for VS 2019/2022* (or run
`vcvars64.bat` inside any cmd), then:

```
build.bat
```

The binary is produced at `build\SoundOverlay.exe`. It is statically
linked to the CRT (`/MT`), subsystem `WINDOWS` (no console), and needs
no runtime dependencies on the target machine.

### With mingw-w64

From an MSYS2 `mingw64` shell or any shell with `x86_64-w64-mingw32-gcc`
aliased to `gcc`:

```
build-mingw.bat
```

## Running

Double-click `SoundOverlay.exe`. The settings window appears:

1. Pick the audio device that your game plays through (the first entry
   tagged `[default]` is what Windows is using right now).
2. Leave **Sensitivity** at `1.0x` for a first try. Lower = more
   triggers, higher = fewer.
3. Pick a size / position for the HUD.
4. Click **Start**. The compass-shaped overlay appears and begins
   lighting up green (`STEP`) and red (`SHOT`) markers as events are
   detected.

Quit with **Ctrl + F10** or by closing the settings window.

## How it works

### Capture (`audio.c`)

WASAPI is initialised in **shared, loopback** mode against the selected
render endpoint. The audio thread pulls packets with
`IAudioCaptureClient::GetBuffer`, detects the mix format from the
endpoint's `WAVEFORMATEXTENSIBLE`, downmixes 1 / 2 / 5.1 / 7.1 layouts
to stereo using ITU-775-style weights, linearly resamples to 48 kHz if
the endpoint runs at a different rate, and pushes the stereo float
frames into a 1-second ring buffer protected by a critical section.

### Detection (`detector.c`)

For each 2048-sample window (1024-sample hop):

1. Sum to mono, apply Hann window, run the radix-2 FFT in `fft.c`.
2. Compute magnitude spectrum; integrate power across four bands and
   compute positive spectral flux (current - previous magnitude,
   clipped at zero) in the high band.
3. Update per-band exponential noise floors (alpha = 0.02, ~0.7 s time
   constant).
4. Compute per-band ratios vs. their floors. Classify:
   - **Gunshot** = big high-band flux + elevated high band + elevated
     ultra band + minimum absolute loudness gate, with 130 ms
     refractory.
   - **Footstep** = elevated low band + positive low-band flux + low
     dominance over high band + moderate mid band, with 100 ms
     refractory.
5. Stereo pan estimated via `(rms_r - rms_l) / (rms_r + rms_l)` on the
   windowed signals.

### Overlay (`overlay.c`)

A `WS_POPUP` layered window sized `size_px * size_px`, positioned by
choice of five anchors, with:

- `WS_EX_LAYERED` + `SetLayeredWindowAttributes(..., LWA_COLORKEY)` for
  chroma-keyed transparency (cheap, no per-pixel alpha needed).
- `WS_EX_TRANSPARENT | WS_EX_NOACTIVATE` so the window is click-through
  and never steals focus.
- `WS_EX_TOPMOST | WS_EX_TOOLWINDOW` so it floats above full-screen
  windowed / borderless games without a taskbar icon.
- A 33 ms timer forces redraws; GDI draws to an offscreen DC that is
  `BitBlt`-ed to the window for flicker-free double buffering.
- Events fade linearly over 1.4 s. Pan maps to an angle on the forward
  hemisphere of the compass.

## Tuning tips

- **Too many false triggers** on music / explosions: raise sensitivity
  toward `1.5x`.
- **Missed footsteps**: lower sensitivity toward `0.7x`.
- Disable game and Windows "surround virtualization" effects on stereo
  output so L/R direction stays crisp.
- If you play with a 5.1 / 7.1 output device selected in Windows, the
  downmix is done internally; direction is still meaningful but
  front/back cues collapse into the left/right axis (stereo can't
  resolve them).

## Known limitations

- Stereo pan cannot distinguish front from back. A true surround
  capture path (7.1 WASAPI shared mode without downmix) is a natural
  extension.
- Heuristic detectors always have false positives on unusual audio
  (nearby explosions, music stingers with strong kicks). A small
  ML classifier trained on labeled game audio would improve precision.
- Windows only. The overlay relies on Win32 layered windows and WASAPI
  loopback.
