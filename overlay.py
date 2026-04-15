"""
overlay.py – Transparent, always-on-top, click-through game overlay.

Uses tkinter + ctypes to create a layered window that renders a circular
sound radar. Indicators appear at the direction of detected sounds and
fade out over EVENT_FADE_SEC seconds.

Compass convention (as displayed):
  •   0° → top   (ahead / North)
  •  90° → right (East)
  • -90° → left  (West)
  • 180° → bottom (South / behind)

The analyser currently supplies ±90° (left/right channel balance).
The remaining axes (front/back) can be wired up when spatial-audio data
is available.
"""

import ctypes
import math
import queue
import time
import tkinter as tk
from dataclasses import dataclass, field
from typing import Any

import config
from sound_analyzer import SoundEvent


# ── Internal render state ─────────────────────────────────────────────────────

@dataclass
class RenderEvent:
    base: SoundEvent
    created_at: float = field(default_factory=time.time)

    def age(self) -> float:
        return time.time() - self.created_at

    def alpha(self) -> float:
        return max(0.0, 1.0 - self.age() / config.EVENT_FADE_SEC)

    def is_expired(self) -> bool:
        return self.age() >= config.EVENT_FADE_SEC


# ── Colour helpers ────────────────────────────────────────────────────────────

def _blend_hex(fg: str, bg: str, t: float) -> str:
    """Linear blend: t=1 → fg, t=0 → bg."""
    def parse(h):
        h = h.lstrip("#")
        return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)

    fr, fg_, fb = parse(fg)
    br, bg_, bb = parse(bg)
    r = int(fr * t + br * (1 - t))
    g = int(fg_ * t + bg_ * (1 - t))
    b = int(fb * t + bb * (1 - t))
    return f"#{r:02x}{g:02x}{b:02x}"


# ── Overlay window ────────────────────────────────────────────────────────────

class Overlay:
    """
    Transparent, click-through, always-on-top radar overlay.

    Call ``start(event_queue)`` to launch the tkinter main loop on the
    calling thread (blocking).  Post SoundEvent objects to *event_queue*
    from any other thread.
    """

    def __init__(self):
        self._events: list[RenderEvent] = []
        self._eq: queue.Queue | None    = None
        self._running = False

        self.root: tk.Tk | None     = None
        self.canvas: tk.Canvas | None = None

        # Geometry
        self.S  = config.OVERLAY_SIZE          # total canvas size
        self.cx = self.S // 2                  # centre x
        self.cy = self.S // 2                  # centre y
        self.R  = self.S // 2 - 12             # outer ring radius
        self.r  = int(self.R * 0.68)           # inner ring radius
        self.indicator_r = int(self.R * 0.80)  # where indicators are placed

    # ── Public ─────────────────────────────────────────────────────────────────

    def start(self, event_queue: queue.Queue):
        """Build the window and run the tkinter main loop (blocking)."""
        self._eq      = event_queue
        self._running = True

        self._build_window()
        self._make_click_through()

        interval_ms = max(1, 1000 // config.OVERLAY_FPS)
        self.root.after(interval_ms, self._tick)
        self.root.mainloop()

    def stop(self):
        self._running = False
        if self.root:
            try:
                self.root.quit()
            except Exception:
                pass

    # ── Window construction ───────────────────────────────────────────────────

    def _build_window(self):
        self.root = tk.Tk()
        self.root.title("Sound Radar")
        self.root.overrideredirect(True)           # borderless
        self.root.wm_attributes("-topmost", True)  # always on top
        self.root.wm_attributes("-alpha", config.OVERLAY_ALPHA)
        self.root.configure(bg="black")

        sw = self.root.winfo_screenwidth()
        sh = self.root.winfo_screenheight()
        x, y = self._position(sw, sh)
        self.root.geometry(f"{self.S}x{self.S}+{x}+{y}")

        self.canvas = tk.Canvas(
            self.root,
            width=self.S, height=self.S,
            bg="black",
            highlightthickness=0,
        )
        self.canvas.pack()

        # Allow dragging the overlay with right-click drag
        self.root.bind("<Button-3>",        self._drag_start)
        self.root.bind("<B3-Motion>",       self._drag_motion)
        # Exit with Escape or middle-click
        self.root.bind("<Escape>",          lambda _: self.stop())
        self.root.bind("<Button-2>",        lambda _: self.stop())

        self._drag_x = 0
        self._drag_y = 0

    def _position(self, sw: int, sh: int) -> tuple[int, int]:
        m = config.OVERLAY_MARGIN
        pos = config.OVERLAY_POSITION.lower()
        if pos == "top":
            return sw // 2 - self.S // 2, m
        elif pos == "left":
            return m, sh // 2 - self.S // 2
        elif pos == "right":
            return sw - self.S - m, sh // 2 - self.S // 2
        elif pos == "center":
            return sw // 2 - self.S // 2, sh // 2 - self.S // 2
        else:  # "bottom" (default)
            return sw // 2 - self.S // 2, sh - self.S - m

    def _make_click_through(self):
        """
        Apply Windows extended-window style flags so the overlay passes
        mouse events through to the game window beneath it.
        WS_EX_LAYERED  = 0x00080000
        WS_EX_TRANSPARENT = 0x00000020
        """
        try:
            self.root.update()
            # GetParent retrieves the real Win32 HWND for a tkinter window
            hwnd = ctypes.windll.user32.GetParent(self.root.winfo_id())
            GWL_EXSTYLE = -20
            WS_EX_LAYERED     = 0x00080000
            WS_EX_TRANSPARENT = 0x00000020
            current = ctypes.windll.user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
            ctypes.windll.user32.SetWindowLongW(
                hwnd, GWL_EXSTYLE, current | WS_EX_LAYERED | WS_EX_TRANSPARENT
            )
        except Exception:
            pass   # Non-Windows – just skip; overlay still works without click-through

    # ── Drag support ──────────────────────────────────────────────────────────

    def _drag_start(self, event: Any):
        self._drag_x = event.x_root - self.root.winfo_x()
        self._drag_y = event.y_root - self.root.winfo_y()

    def _drag_motion(self, event: Any):
        x = event.x_root - self._drag_x
        y = event.y_root - self._drag_y
        self.root.geometry(f"+{x}+{y}")

    # ── Main loop tick ────────────────────────────────────────────────────────

    def _tick(self):
        if not self._running:
            return

        # Drain the event queue
        try:
            while True:
                ev: SoundEvent = self._eq.get_nowait()
                self._events.append(RenderEvent(ev))
        except queue.Empty:
            pass

        # Remove expired events
        self._events = [e for e in self._events if not e.is_expired()]

        self._draw()

        interval_ms = max(1, 1000 // config.OVERLAY_FPS)
        self.root.after(interval_ms, self._tick)

    # ── Drawing ───────────────────────────────────────────────────────────────

    def _draw(self):
        c = self.canvas
        c.delete("all")

        self._draw_background()
        self._draw_grid()
        self._draw_cardinal_labels()
        self._draw_events()
        self._draw_center_dot()
        self._draw_legend()

    def _draw_background(self):
        c = self.canvas
        cx, cy, R = self.cx, self.cy, self.R
        c.create_oval(cx-R, cy-R, cx+R, cy+R,
                      fill=config.COL_BG, outline=config.COL_RING, width=2)

    def _draw_grid(self):
        c = self.canvas
        cx, cy = self.cx, self.cy

        # Inner dashed ring
        r = self.r
        c.create_oval(cx-r, cy-r, cx+r, cy+r,
                      fill="", outline=config.COL_GRID, width=1, dash=(3, 6))

        # Cross-hair ticks at cardinal points
        tick = 6
        R = self.R
        for a in (0, 90, 180, 270):
            rad = math.radians(a - 90)
            x1 = cx + (R - tick) * math.cos(rad)
            y1 = cy + (R - tick) * math.sin(rad)
            x2 = cx + R * math.cos(rad)
            y2 = cy + R * math.sin(rad)
            c.create_line(x1, y1, x2, y2, fill=config.COL_GRID, width=1)

    def _draw_cardinal_labels(self):
        c  = self.canvas
        cx, cy = self.cx, self.cy
        R  = self.R
        f  = ("Consolas", 8, "bold")
        offset = 14

        for label, deg in (("N", 0), ("E", 90), ("S", 180), ("W", 270)):
            rad = math.radians(deg - 90)
            x   = cx + (R - offset) * math.cos(rad)
            y   = cy + (R - offset) * math.sin(rad)
            c.create_text(x, y, text=label, fill=config.COL_LABEL, font=f)

    def _draw_center_dot(self):
        cx, cy = self.cx, self.cy
        dr = 5
        self.canvas.create_oval(cx-dr, cy-dr, cx+dr, cy+dr,
                                 fill=config.COL_CENTER, outline="")
        # small inner circle
        di = 2
        self.canvas.create_oval(cx-di, cy-di, cx+di, cy+di,
                                 fill=config.COL_BG, outline="")

    def _draw_events(self):
        for rev in self._events:
            self._draw_single_event(rev)

    def _draw_single_event(self, rev: RenderEvent):
        c   = self.canvas
        ev  = rev.base
        a   = rev.alpha()
        cx, cy = self.cx, self.cy
        ir  = self.indicator_r

        # Convert angle to screen radians (0° = top = −90° in math convention)
        rad = math.radians(ev.angle_deg - 90)
        ex  = cx + ir * math.cos(rad)
        ey  = cy + ir * math.sin(rad)

        if ev.kind == "gunshot":
            self._draw_gunshot(c, cx, cy, ex, ey, a, ev.intensity)
        elif ev.kind == "footstep":
            self._draw_footstep(c, cx, cy, ex, ey, a, ev.intensity)
        elif ev.kind == "vehicle":
            self._draw_vehicle(c, cx, cy, ex, ey, a)

    def _draw_gunshot(self, c, cx, cy, ex, ey, a, intensity):
        """Pulsing ring + dashed line."""
        outer = int(14 + intensity * 10)
        inner = outer // 2

        col1 = _blend_hex(config.COL_GUNSHOT,  config.COL_BG, a)
        col2 = _blend_hex(config.COL_GUNSHOT2, config.COL_BG, a)

        # Dashed direction line
        line_col = _blend_hex(config.COL_GUNSHOT, config.COL_BG, a * 0.55)
        c.create_line(cx, cy, ex, ey, fill=line_col, width=1, dash=(5, 5))

        # Outer ring
        c.create_oval(ex-outer, ey-outer, ex+outer, ey+outer,
                      fill="", outline=col1, width=2)
        # Inner flash (bright when fresh)
        if a > 0.5:
            c.create_oval(ex-inner, ey-inner, ex+inner, ey+inner,
                          fill=col2, outline=col1, width=1)

        # "GUN" label beneath
        if a > 0.3:
            lx = ex + (ex - cx) * 0.18
            ly = ey + (ey - cy) * 0.18
            c.create_text(lx, ly, text="GUN",
                          fill=col1, font=("Consolas", 6, "bold"))

    def _draw_footstep(self, c, cx, cy, ex, ey, a, intensity):
        """Diamond marker + subtle line."""
        sz = int(7 + intensity * 5)

        col  = _blend_hex(config.COL_FOOTSTEP, config.COL_BG, a)
        lcol = _blend_hex(config.COL_FOOTSTEP, config.COL_BG, a * 0.25)

        # Subtle direction line
        c.create_line(cx, cy, ex, ey, fill=lcol, width=1, dash=(2, 8))

        # Diamond
        c.create_polygon(
            ex,    ey-sz,
            ex+sz, ey,
            ex,    ey+sz,
            ex-sz, ey,
            fill="", outline=col, width=2,
        )

        if a > 0.5:
            di = sz // 3
            c.create_oval(ex-di, ey-di, ex+di, ey+di, fill=col, outline="")

    def _draw_vehicle(self, c, cx, cy, ex, ey, a):
        """Small hexagon for vehicle rumble."""
        sz  = 8
        col = _blend_hex(config.COL_VEHICLE, config.COL_BG, a)
        pts = []
        for i in range(6):
            ang = math.radians(i * 60)
            pts.extend([ex + sz * math.cos(ang), ey + sz * math.sin(ang)])
        c.create_polygon(pts, fill="", outline=col, width=2)

    def _draw_legend(self):
        c = self.canvas
        S = self.S
        f = ("Consolas", 7)

        items = [
            ("◆ Footstep",  config.COL_FOOTSTEP),
            ("○ Gunshot",   config.COL_GUNSHOT),
            ("⬡ Vehicle",   config.COL_VEHICLE),
        ]
        for i, (text, col) in enumerate(items):
            c.create_text(8, 8 + i * 13, text=text, fill=col,
                          font=f, anchor="w")

        # Title
        c.create_text(S // 2, S - 8, text="SOUND RADAR",
                      fill=config.COL_LABEL, font=("Consolas", 7))
        # Hint
        c.create_text(S - 4, S - 8, text="RMB drag • ESC quit",
                      fill=config.COL_GRID, font=("Consolas", 6), anchor="e")
