#include "overlay.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define OVERLAY_CLASS L"SoundOverlayHUD"
#define EVENT_FADE_SEC 1.4f
#define EVENT_RING_CAP 64
#define OVERLAY_TIMER  1

/* We use chroma-key transparency via SetLayeredWindowAttributes so plain
 * GDI drawing works. The color key must appear nowhere in our markers. */
static const COLORREF CHROMA_KEY = RGB(1, 2, 3);

typedef struct {
    SoundEvent ev;
    double     added_sec;
} TimedEvent;

struct Overlay {
    HINSTANCE inst;
    HWND      hwnd;
    int       size;
    OverlayPosition pos;

    CRITICAL_SECTION lock;
    TimedEvent       events[EVENT_RING_CAP];
    int              event_count;

    double qpc_freq;
};

static double now_seconds(const Overlay *o) {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / o->qpc_freq;
}

static void place_window(Overlay *o) {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int m = 24;
    int x = m, y = m;
    switch (o->pos) {
        case OP_TOP_LEFT:     x = m;              y = m; break;
        case OP_TOP_RIGHT:    x = sw - o->size - m; y = m; break;
        case OP_BOTTOM_LEFT:  x = m;              y = sh - o->size - m - 40; break;
        case OP_BOTTOM_RIGHT: x = sw - o->size - m; y = sh - o->size - m - 40; break;
        case OP_CENTER:       x = (sw - o->size) / 2; y = (sh - o->size) / 2; break;
    }
    SetWindowPos(o->hwnd, HWND_TOPMOST, x, y, o->size, o->size,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void draw_compass(HDC dc, int cx, int cy, int radius) {
    HPEN ring_pen = CreatePen(PS_SOLID, 2, RGB(50, 50, 50));
    HPEN inner_pen = CreatePen(PS_SOLID, 1, RGB(30, 30, 30));
    HGDIOBJ old_pen = SelectObject(dc, ring_pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
    SelectObject(dc, inner_pen);
    Ellipse(dc, cx - radius / 2, cy - radius / 2,
                cx + radius / 2, cy + radius / 2);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(90, 90, 90));
    HFONT font = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, font);

    static const struct { const wchar_t *lbl; int ang; } labels[] = {
        { L"F",  -90 }, { L"R",    0 },
        { L"B",   90 }, { L"L",  180 },
    };
    for (int i = 0; i < 4; ++i) {
        double a = labels[i].ang * M_PI / 180.0;
        int tx = cx + (int)((radius + 10) * cos(a));
        int ty = cy + (int)((radius + 10) * sin(a));
        RECT r = { tx - 10, ty - 8, tx + 10, ty + 8 };
        DrawTextW(dc, labels[i].lbl, 1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    SelectObject(dc, old_font); DeleteObject(font);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(ring_pen);
    DeleteObject(inner_pen);
}

static COLORREF fade_color(COLORREF base, float alpha) {
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    int r = (int)(GetRValue(base) * alpha);
    int g = (int)(GetGValue(base) * alpha);
    int b = (int)(GetBValue(base) * alpha);
    /* Avoid the chroma key by nudging very-dark colors. */
    if (r == 1 && g == 2 && b == 3) { r = 2; }
    if (r == 0 && g == 0 && b == 0) { r = 1; g = 1; b = 1; }
    return RGB(r, g, b);
}

static void draw_footstep(HDC dc, int x, int y, float strength, float alpha) {
    COLORREF col = fade_color(RGB(34, 230, 130), alpha);
    int size = (int)(10 + 4 * strength);
    HBRUSH b = CreateSolidBrush(col);
    HPEN   p = CreatePen(PS_SOLID, 2, col);
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, p);
    Ellipse(dc, x - size, y - size, x + size, y + size);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, x - size - 5, y - size - 5, x + size + 5, y + size + 5);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, col);
    HFONT font = CreateFontW(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ of = SelectObject(dc, font);
    RECT r = { x - 30, y - size - 22, x + 30, y - size - 8 };
    DrawTextW(dc, L"STEP", 4, &r, DT_CENTER | DT_SINGLELINE);
    SelectObject(dc, of); DeleteObject(font);
}

static void draw_gunshot(HDC dc, int x, int y, float strength, float alpha) {
    COLORREF col = fade_color(RGB(255, 64, 80), alpha);
    int size = (int)(14 + 6 * strength);
    HPEN p = CreatePen(PS_SOLID, 3, col);
    HGDIOBJ op = SelectObject(dc, p);
    for (int k = 0; k < 8; ++k) {
        double a = k * 45.0 * M_PI / 180.0;
        int ex = x + (int)(size * cos(a));
        int ey = y + (int)(size * sin(a));
        MoveToEx(dc, x, y, NULL);
        LineTo(dc, ex, ey);
    }
    SelectObject(dc, op); DeleteObject(p);

    HBRUSH b = CreateSolidBrush(col);
    HGDIOBJ ob = SelectObject(dc, b);
    Ellipse(dc, x - 4, y - 4, x + 4, y + 4);
    SelectObject(dc, ob); DeleteObject(b);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, col);
    HFONT font = CreateFontW(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ of = SelectObject(dc, font);
    RECT r = { x - 30, y - size - 20, x + 30, y - size - 6 };
    DrawTextW(dc, L"SHOT", 4, &r, DT_CENTER | DT_SINGLELINE);
    SelectObject(dc, of); DeleteObject(font);
}

static void paint(Overlay *o, HDC win_dc, RECT *client) {
    int W = client->right - client->left;
    int H = client->bottom - client->top;

    /* Double buffer. */
    HDC mem = CreateCompatibleDC(win_dc);
    HBITMAP bmp = CreateCompatibleBitmap(win_dc, W, H);
    HGDIOBJ ob = SelectObject(mem, bmp);

    /* Fill with chroma key. */
    HBRUSH key_brush = CreateSolidBrush(CHROMA_KEY);
    RECT full = { 0, 0, W, H };
    FillRect(mem, &full, key_brush);
    DeleteObject(key_brush);

    int cx = W / 2, cy = H / 2;
    int radius = (W < H ? W : H) / 2 - 22;
    if (radius < 20) radius = 20;

    draw_compass(mem, cx, cy, radius);

    /* Snapshot events under lock, keep only fresh ones. */
    TimedEvent local[EVENT_RING_CAP];
    int n = 0;
    EnterCriticalSection(&o->lock);
    double t_now = now_seconds(o);
    int keep = 0;
    for (int i = 0; i < o->event_count; ++i) {
        if ((float)(t_now - o->events[i].added_sec) < EVENT_FADE_SEC) {
            local[n++] = o->events[i];
            o->events[keep++] = o->events[i];
        }
    }
    o->event_count = keep;
    LeaveCriticalSection(&o->lock);

    for (int i = 0; i < n; ++i) {
        TimedEvent *te = &local[i];
        float age = (float)(t_now - te->added_sec);
        float alpha = 1.0f - age / EVENT_FADE_SEC;
        if (alpha < 0.0f) alpha = 0.0f;

        double ang_deg = -90.0 + te->ev.pan * 90.0;
        double a = ang_deg * M_PI / 180.0;
        int px = cx + (int)((radius - 6) * cos(a));
        int py = cy + (int)((radius - 6) * sin(a));

        if (te->ev.kind == SE_FOOTSTEP) {
            draw_footstep(mem, px, py, te->ev.strength, alpha);
        } else {
            draw_gunshot(mem, px, py, te->ev.strength, alpha);
        }
    }

    BitBlt(win_dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);

    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static LRESULT CALLBACK overlay_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Overlay *o = (Overlay *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_PAINT: {
            if (!o) break;
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            paint(o, dc, &rc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            if (wp == OVERLAY_TIMER) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_NCHITTEST:
            /* Redundant with WS_EX_TRANSPARENT, but doesn't hurt. */
            return HTTRANSPARENT;
        case WM_DESTROY:
            KillTimer(hwnd, OVERLAY_TIMER);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void register_class_once(HINSTANCE inst) {
    static int done = 0;
    if (done) return;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = overlay_wndproc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = OVERLAY_CLASS;
    RegisterClassExW(&wc);
    done = 1;
}

Overlay *overlay_create(HINSTANCE inst, int size_px, OverlayPosition pos) {
    register_class_once(inst);
    Overlay *o = (Overlay *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->inst = inst;
    o->size = size_px;
    o->pos  = pos;
    InitializeCriticalSection(&o->lock);
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    o->qpc_freq = (double)f.QuadPart;

    DWORD ex = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST
             | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    DWORD st = WS_POPUP;

    o->hwnd = CreateWindowExW(ex, OVERLAY_CLASS, L"SoundOverlay", st,
                              0, 0, size_px, size_px,
                              NULL, NULL, inst, NULL);
    if (!o->hwnd) {
        DeleteCriticalSection(&o->lock);
        free(o);
        return NULL;
    }
    SetWindowLongPtrW(o->hwnd, GWLP_USERDATA, (LONG_PTR)o);
    SetLayeredWindowAttributes(o->hwnd, CHROMA_KEY, 0, LWA_COLORKEY);

    place_window(o);
    SetTimer(o->hwnd, OVERLAY_TIMER, 33, NULL);
    return o;
}

void overlay_destroy(Overlay *o) {
    if (!o) return;
    if (o->hwnd) DestroyWindow(o->hwnd);
    DeleteCriticalSection(&o->lock);
    free(o);
}

void overlay_add_event(Overlay *o, const SoundEvent *e) {
    if (!o || !e) return;
    EnterCriticalSection(&o->lock);
    if (o->event_count < EVENT_RING_CAP) {
        o->events[o->event_count].ev = *e;
        o->events[o->event_count].added_sec = now_seconds(o);
        o->event_count++;
    } else {
        /* Shift out oldest. */
        memmove(&o->events[0], &o->events[1],
                sizeof(TimedEvent) * (EVENT_RING_CAP - 1));
        o->events[EVENT_RING_CAP - 1].ev = *e;
        o->events[EVENT_RING_CAP - 1].added_sec = now_seconds(o);
    }
    LeaveCriticalSection(&o->lock);
}

void overlay_reconfigure(Overlay *o, int size_px, OverlayPosition pos) {
    if (!o) return;
    o->size = size_px;
    o->pos = pos;
    place_window(o);
    InvalidateRect(o->hwnd, NULL, TRUE);
}

void overlay_show(Overlay *o) {
    if (!o) return;
    ShowWindow(o->hwnd, SW_SHOWNOACTIVATE);
}

void overlay_hide(Overlay *o) {
    if (!o) return;
    ShowWindow(o->hwnd, SW_HIDE);
}

HWND overlay_hwnd(const Overlay *o) {
    return o ? o->hwnd : NULL;
}
