# SoundOverlay

A native C / Win32 game audio visualizer that captures your PC's audio
output via WASAPI loopback and shows a transparent, click-through HUD
with directional markers for **footsteps**, **gunshots**, **vehicles**,
and **explosions**.

No external dependencies. Pure C, pure Win32. Single `.exe`.

## Supported Games (built-in profiles)

| Profile | Tuned for | Key features |
|---------|-----------|-------------|
| **Universal** | Any game | Balanced defaults |
| **Fortnite** | Fortnite | Wide footstep band, build/edit awareness |
| **Call of Duty: Warzone** | Warzone / MW | Heavy bass compensation, vehicle detection |
| **Valorant** | Valorant | Crisp footsteps, no vehicle noise |
| **Counter-Strike 2** | CS2 | Tight thresholds, fast cooldowns |
| **Apex Legends** | Apex | Legend-varied footsteps, vehicle rumble |
| **PUBG: Battlegrounds** | PUBG | Long-range shots, vehicle emphasis |
| **Rainbow Six Siege** | R6 Siege | Breach sounds, precise footsteps |
| **Overwatch 2** | OW2 | Ability-aware, varied heroes |
| **Escape from Tarkov** | Tarkov | Ultra-sensitive, realistic audio |

Each profile tunes frequency bands, detection thresholds, cooldowns,
noise-floor adaptation speed, and default overlay settings.

## Features

- **WASAPI loopback** capture of any render endpoint — no virtual cable
  needed
- **4 event types**: footsteps (green), gunshots (red), vehicles (blue),
  explosions (orange) with distinct HUD markers
- **FFT-based detection** with spectral flux onset detection, adaptive
  per-band noise floors, and refractory periods
- **10 game profiles** with per-game tuned frequency bands and thresholds
- **Launcher / settings GUI**:
  - Game selection buttons (click to switch profile, even while running)
  - Audio device picker
  - Sensitivity slider (0.5x - 2.0x)
  - Overlay size slider (200 - 600 px)
  - Position selector (5 positions)
  - Per-event-type enable/disable checkboxes
  - Show/hide overlay toggle
  - Minimize-to-tray option
  - Live event log with timestamps and direction
  - Running detection stats and session timer
- **Transparent overlay**: `WS_EX_LAYERED | WS_EX_TRANSPARENT |
  WS_EX_TOPMOST` with chroma-key, double-buffered GDI at ~30 fps
- **System tray**: right-click for Show/Hide, Start/Stop, Exit
- **Global hotkey**: `Ctrl + F10` to quit from anywhere

## Source layout

```
fft.c/h         Radix-2 in-place FFT
audio.c/h       WASAPI loopback capture, downmix, resample, ring buffer
detector.c/h    FFT analysis + 4-type classifier with profile params
overlay.c/h     Layered click-through HUD with 4 marker shapes
profiles.c/h    10 game profiles with tuned detection parameters
main.c          Launcher GUI, tray, event log, pipeline orchestration
build.bat       MSVC build
build-mingw.bat mingw-w64 build
```

## Building

### MSVC (recommended)

Open an **x64 Native Tools Command Prompt** for VS 2019/2022:

```
build.bat
```

Output: `build\SoundOverlay.exe` (static CRT, no console, no dependencies).

### mingw-w64

From MSYS2 mingw64 or any shell with `gcc` = `x86_64-w64-mingw32-gcc`:

```
build-mingw.bat
```

## Usage

1. Launch `SoundOverlay.exe`
2. Click a **game profile** button (or leave on Universal)
3. Pick your **audio device** (the one your game plays through)
4. Adjust **sensitivity** if needed (lower = more triggers)
5. Toggle which event types to detect
6. Click **Start**

The compass-shaped overlay appears with:
- **STEP** (green circle) — footsteps
- **SHOT** (red starburst) — gunshots
- **VEH** (blue diamond) — vehicles
- **BOOM** (orange multi-ray) — explosions

All markers are positioned on the compass ring based on stereo pan
(left/right) and fade out over 1.4 seconds.

The **event log** in the launcher shows every detection with timestamp,
type, and direction. Stats at the bottom track total counts and session
duration.

### Controls

| Action | How |
|--------|-----|
| Quit | Ctrl + F10 (global) or close the window |
| Minimize to tray | Check "Minimize to tray", then minimize |
| Tray menu | Right-click the tray icon |
| Switch game mid-session | Click a different game button |
| Adjust sensitivity live | Drag the slider while running |

## How detection works

1. **Capture**: WASAPI loopback at 48 kHz stereo, 1-second ring buffer
2. **Window**: 2048-sample Hann window, 1024-sample hop (50% overlap)
3. **FFT**: Radix-2 in-place, magnitude spectrum
4. **Bands**: Profile-defined frequency ranges for each event type
5. **Noise floor**: Per-band exponential moving average (profile-tuned alpha)
6. **Classification**:
   - Gunshot = high spectral flux + elevated gun band + ultra band + loudness gate
   - Explosion = elevated explosion band + positive onset flux + high absolute energy
   - Footstep = elevated foot band + onset + low-dominance over gun band
   - Vehicle = sustained sub-bass rumble above threshold
7. **Direction**: Energy-weighted L/R RMS ratio → pan angle on compass

## Tuning tips

- **Too many false positives**: raise sensitivity toward 1.5x
- **Missing events**: lower toward 0.7x
- **Specific event noise**: uncheck that event type
- **Best direction**: use stereo output, disable surround virtualization
- Switch profiles when switching games — each profile's frequency bands
  match that game's audio mix

## Known limitations

- Stereo cannot resolve front vs. back (only left/right/center)
- Heuristic classification has inherent false positives on unusual audio
- Windows only (WASAPI + Win32 layered windows)
