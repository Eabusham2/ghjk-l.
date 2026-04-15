# Sound Detection Game Overlay

A transparent, always-on-top radar overlay for Windows that listens to your
system audio, detects in-game sounds (footsteps, gunshots, vehicles), and
shows you the **direction** of each sound on a circular compass.

Works with Fortnite, Warzone, Apex Legends, PUBG, and any other game that
outputs stereo audio through Windows.

---

## What It Detects

| Icon | Sound | How |
|------|-------|-----|
| `◆` | Footstep | Mid-bass spike (80–500 Hz) above ambient noise |
| `○` | Gunshot  | Loud transient with broadband high-frequency content |
| `⬡` | Vehicle  | Sustained sub-bass rumble (30–200 Hz) |

**Direction** is derived from the left / right stereo balance — sounds panned
hard-left appear on the left side of the compass, right-panned sounds on the
right.

---

## Quick Start

### Run from source

```bash
# 1. Install Python 3.10+ (https://www.python.org)
# 2. Install dependencies
pip install -r requirements.txt

# 3. Launch overlay
python main.py
```

### Build a standalone EXE

```bat
pip install -r requirements.txt
build.bat
```

The compiled binary appears at `dist\SoundOverlay.exe` — no Python installation
required on the target PC.

---

## Command-Line Options

```
python main.py [options]

  --pos    {bottom,top,left,right,center}   Radar position  (default: bottom)
  --size   N                                Radar diameter in px (default: 260)
  --alpha  0.0–1.0                          Window opacity    (default: 0.88)
  --fade   seconds                          Indicator lifetime (default: 2.5)
```

Examples:
```bat
# Put the radar in the top-right area, slightly more transparent
python main.py --pos top --alpha 0.7

# Bigger radar, indicators stay longer
python main.py --size 320 --fade 3.5
```

---

## Controls (in-game)

| Action | Shortcut |
|--------|----------|
| Move overlay | Right-click drag |
| Quit | `Escape` or middle-click |

---

## How It Works

```
 Windows speakers / headphones
          │
          ▼
  AudioCapture (WASAPI loopback)
          │  raw stereo float32 chunks
          ▼
  SoundAnalyzer (numpy FFT)
    • amplitude spike detection
    • frequency band analysis
    • L/R channel balance → direction
          │  SoundEvent objects
          ▼
  Overlay (tkinter + ctypes)
    • transparent layered window
    • WS_EX_TRANSPARENT → click-through
    • WS_EX_TOPMOST → above game
    • compass compass with fading indicators
```

### WASAPI Loopback

The overlay uses Windows Audio Session API (WASAPI) loopback to capture
whatever is playing through your audio device — this includes game audio
without needing any special driver or game integration.

If your audio device does not expose a loopback endpoint automatically, enable
**Stereo Mix** in Windows Sound settings:

1. Right-click the speaker icon → Sound settings
2. Recording tab → right-click blank area → *Show Disabled Devices*
3. Enable **Stereo Mix**

---

## Tuning Detection Sensitivity

Edit `config.py` to adjust thresholds:

```python
GUNSHOT_AMPLITUDE_THRESH  = 0.45   # lower = more sensitive to gunshots
FOOTSTEP_AMPLITUDE_THRESH = 0.04   # lower = more sensitive to footsteps
GUNSHOT_COOLDOWN_SEC      = 0.35   # increase if gunshots double-trigger
FOOTSTEP_COOLDOWN_SEC     = 0.22   # increase if footsteps flood the radar
```

---

## Requirements

- Windows 10 / 11
- Python 3.10+ (or use the pre-built EXE)
- `sounddevice`, `numpy` (installed via `requirements.txt`)

---

## Limitations

- **Direction resolution**: Only left/right is derived from standard stereo.
  Front/back differentiation requires HRTF or surround sound metadata.
- **Game audio mixing**: If the game uses dynamic audio compression or mono
  mixing, channel balance may not reflect actual in-game direction precisely.
- **Detection accuracy**: Thresholds are tuned for typical shooter audio; very
  quiet sounds or extremely compressed audio streams may be missed.
