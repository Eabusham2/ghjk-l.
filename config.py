"""
Sound Detection Game Overlay - Configuration
"""

# ── Audio ─────────────────────────────────────────────────────────────────────
SAMPLE_RATE = 44100        # Hz
CHUNK_SIZE  = 1024         # samples per analysis window
CHANNELS    = 2            # stereo

# ── Detection thresholds (0.0–1.0 normalised amplitude) ───────────────────────
GUNSHOT_AMPLITUDE_THRESH   = 0.45   # minimum peak to trigger gunshot check
GUNSHOT_SPIKE_RATIO        = 2.5    # peak must be N× the rolling average
GUNSHOT_HIGH_FREQ_RATIO    = 0.25   # fraction of energy above 1 kHz
GUNSHOT_COOLDOWN_SEC       = 0.35   # ignore further gunshots within this window

FOOTSTEP_AMPLITUDE_THRESH  = 0.04   # minimum peak to trigger footstep check
FOOTSTEP_LOW_FREQ_RATIO    = 0.38   # fraction of energy in 80–500 Hz band
FOOTSTEP_COOLDOWN_SEC      = 0.22   # ignore further footsteps within this window

# Rolling history length (chunks) for baseline amplitude estimation
AMPLITUDE_HISTORY_LEN = 30

# ── Overlay geometry & appearance ─────────────────────────────────────────────
OVERLAY_SIZE        = 260           # pixel diameter of the radar
OVERLAY_ALPHA       = 0.88          # window opacity (0=invisible, 1=opaque)
OVERLAY_POSITION    = "bottom"      # "bottom" | "top" | "left" | "right" | "center"
OVERLAY_MARGIN      = 40            # px from screen edge

EVENT_FADE_SEC      = 2.5           # how long an indicator stays visible

# Colours (hex)
COL_BG          = "#0d0d1a"
COL_RING        = "#1a1a33"
COL_GRID        = "#222244"
COL_LABEL       = "#55557a"
COL_CENTER      = "#e0e0ff"
COL_FOOTSTEP    = "#00ff99"
COL_GUNSHOT     = "#ff3333"
COL_GUNSHOT2    = "#ff8800"         # inner flash
COL_VEHICLE     = "#44aaff"

# Refresh rate
OVERLAY_FPS = 30
