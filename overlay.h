/* Transparent, click-through, always-on-top overlay window. */
#ifndef SOUND_OVERLAY_OVERLAY_H
#define SOUND_OVERLAY_OVERLAY_H

#include <windows.h>
#include "detector.h"

typedef enum {
    OP_TOP_LEFT = 0,
    OP_TOP_RIGHT,
    OP_BOTTOM_LEFT,
    OP_BOTTOM_RIGHT,
    OP_CENTER,
} OverlayPosition;

typedef struct Overlay Overlay;

Overlay *overlay_create(HINSTANCE inst, int size_px, OverlayPosition pos);
void     overlay_destroy(Overlay *o);

/* Push a new event; safe to call from any thread. */
void overlay_add_event(Overlay *o, const SoundEvent *e);

/* Reconfigure size/position; safe from the UI thread. */
void overlay_reconfigure(Overlay *o, int size_px, OverlayPosition pos);

void overlay_show(Overlay *o);
void overlay_hide(Overlay *o);

HWND overlay_hwnd(const Overlay *o);

#endif
