/* SoundOverlay - settings window + pipeline orchestrator.
 *
 * The main window is a small control panel:
 *   - Device combobox (audio render endpoint to capture in loopback)
 *   - Sensitivity slider (0.5x .. 2.0x threshold multiplier)
 *   - Overlay size slider (200 .. 600 px)
 *   - Position combobox (top-right, top-left, bottom-right, bottom-left, center)
 *   - Show-overlay checkbox
 *   - Start / Stop buttons
 *   - Status line
 *
 * When Start is pressed, the audio capture thread begins pulling data
 * from the selected endpoint via WASAPI loopback. A dedicated detector
 * thread pulls 2048-sample windows from the ring, runs FFT-based
 * analysis, and posts events to the overlay window.
 */
#define UNICODE
#define _UNICODE
#define COBJMACROS

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "detector.h"
#include "overlay.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define APP_CLASS   L"SoundOverlaySettings"
#define APP_TITLE   L"SoundOverlay - Footstep / Gunshot Visualizer"

#define ID_CB_DEVICE      1001
#define ID_SL_SENS        1002
#define ID_SL_SIZE        1003
#define ID_CB_POS         1004
#define ID_CK_SHOW        1005
#define ID_BTN_START      1006
#define ID_BTN_STOP       1007
#define ID_STATIC_SENS_V  1010
#define ID_STATIC_SIZE_V  1011
#define ID_STATIC_STATUS  1012

#define WM_APP_STATUS (WM_APP + 1)

typedef struct {
    HINSTANCE        inst;
    HWND             main_wnd;
    HWND             cb_device, sl_sens, sl_size, cb_pos, ck_show;
    HWND             btn_start, btn_stop;
    HWND             lbl_sens, lbl_size, lbl_status;

    AudioDeviceInfo  devices[AUDIO_MAX_DEVICES];
    int              device_count;

    Overlay         *overlay;
    AudioCapture    *capture;
    SoundDetector    detector;
    int              detector_ready;

    HANDLE           det_thread;
    volatile LONG    det_running;
    volatile LONG    det_stop;

    /* Global hotkey: Ctrl+F10. */
    int              hotkey_id;
} App;

static App g_app;

/* ---- detector thread ------------------------------------------------- */

static DWORD WINAPI detector_thread(LPVOID arg) {
    App *a = (App *)arg;
    float *left  = (float *)malloc(sizeof(float) * AUDIO_FFT_FRAMES);
    float *right = (float *)malloc(sizeof(float) * AUDIO_FFT_FRAMES);
    if (!left || !right) { free(left); free(right); return 1; }

    while (!InterlockedCompareExchange(&a->det_stop, 0, 0)) {
        int rc = audio_capture_next_window(a->capture, 200, left, right);
        if (rc < 0) break;
        if (rc == 0) continue;

        SoundEvent ev[2];
        int n = detector_analyze(&a->detector, left, right, ev, 2);
        for (int i = 0; i < n; ++i) {
            overlay_add_event(a->overlay, &ev[i]);
        }
    }

    free(left); free(right);
    InterlockedExchange(&a->det_running, 0);
    return 0;
}

/* ---- UI helpers ------------------------------------------------------ */

static void set_status(App *a, const wchar_t *s) {
    SetWindowTextW(a->lbl_status, s);
}

static int selected_device_index(App *a) {
    int idx = (int)SendMessageW(a->cb_device, CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= a->device_count) return 0;
    return idx;
}

static OverlayPosition selected_position(App *a) {
    int idx = (int)SendMessageW(a->cb_pos, CB_GETCURSEL, 0, 0);
    switch (idx) {
        case 0: return OP_TOP_RIGHT;
        case 1: return OP_TOP_LEFT;
        case 2: return OP_BOTTOM_RIGHT;
        case 3: return OP_BOTTOM_LEFT;
        case 4: return OP_CENTER;
        default: return OP_TOP_RIGHT;
    }
}

static float slider_sensitivity(App *a) {
    /* Slider range 5..20, meaning 0.5 .. 2.0. */
    int v = (int)SendMessageW(a->sl_sens, TBM_GETPOS, 0, 0);
    if (v < 5)  v = 5;
    if (v > 20) v = 20;
    return (float)v / 10.0f;
}

static int slider_size(App *a) {
    int v = (int)SendMessageW(a->sl_size, TBM_GETPOS, 0, 0);
    if (v < 200) v = 200;
    if (v > 600) v = 600;
    return v;
}

static void update_slider_labels(App *a) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%.1fx", slider_sensitivity(a));
    SetWindowTextW(a->lbl_sens, buf);
    swprintf(buf, 64, L"%d px", slider_size(a));
    SetWindowTextW(a->lbl_size, buf);
}

static void populate_devices(App *a) {
    SendMessageW(a->cb_device, CB_RESETCONTENT, 0, 0);
    a->device_count = audio_list_devices(a->devices, AUDIO_MAX_DEVICES);
    for (int i = 0; i < a->device_count; ++i) {
        wchar_t label[320];
        if (a->devices[i].is_default) {
            swprintf(label, 320, L"[default] %s", a->devices[i].name);
        } else {
            swprintf(label, 320, L"%s", a->devices[i].name);
        }
        SendMessageW(a->cb_device, CB_ADDSTRING, 0, (LPARAM)label);
    }
    if (a->device_count > 0) {
        SendMessageW(a->cb_device, CB_SETCURSEL, 0, 0);
    }
}

/* ---- start / stop pipeline ------------------------------------------ */

static void stop_pipeline(App *a) {
    InterlockedExchange(&a->det_stop, 1);
    if (a->det_thread) {
        WaitForSingleObject(a->det_thread, 2000);
        CloseHandle(a->det_thread);
        a->det_thread = NULL;
    }
    if (a->capture) {
        audio_capture_stop(a->capture);
        audio_capture_destroy(a->capture);
        a->capture = NULL;
    }
    if (a->detector_ready) {
        detector_free(&a->detector);
        a->detector_ready = 0;
    }
    if (a->overlay) {
        overlay_hide(a->overlay);
    }
    EnableWindow(a->btn_start, TRUE);
    EnableWindow(a->btn_stop,  FALSE);
    set_status(a, L"Stopped.");
}

static int start_pipeline(App *a) {
    if (a->device_count <= 0) {
        set_status(a, L"No render devices found.");
        return -1;
    }
    int di = selected_device_index(a);
    const wchar_t *id = a->devices[di].id;

    if (detector_init(&a->detector, slider_sensitivity(a)) != 0) {
        set_status(a, L"Failed to initialize detector.");
        return -1;
    }
    a->detector_ready = 1;

    a->capture = audio_capture_create(id);
    if (!a->capture || audio_capture_start(a->capture) != 0) {
        set_status(a, L"Failed to start capture.");
        detector_free(&a->detector); a->detector_ready = 0;
        if (a->capture) { audio_capture_destroy(a->capture); a->capture = NULL; }
        return -1;
    }

    if (!a->overlay) {
        a->overlay = overlay_create(a->inst, slider_size(a), selected_position(a));
        if (!a->overlay) {
            set_status(a, L"Failed to create overlay.");
            audio_capture_stop(a->capture);
            audio_capture_destroy(a->capture); a->capture = NULL;
            detector_free(&a->detector); a->detector_ready = 0;
            return -1;
        }
    } else {
        overlay_reconfigure(a->overlay, slider_size(a), selected_position(a));
    }
    if (IsDlgButtonChecked(a->main_wnd, ID_CK_SHOW) == BST_CHECKED) {
        overlay_show(a->overlay);
    } else {
        overlay_hide(a->overlay);
    }

    a->det_stop = 0;
    InterlockedExchange(&a->det_running, 1);
    a->det_thread = CreateThread(NULL, 0, detector_thread, a, 0, NULL);
    if (!a->det_thread) {
        stop_pipeline(a);
        set_status(a, L"Failed to start detector thread.");
        return -1;
    }

    EnableWindow(a->btn_start, FALSE);
    EnableWindow(a->btn_stop,  TRUE);
    set_status(a, L"Listening. Press Ctrl+F10 to quit.");
    return 0;
}

/* ---- window construction -------------------------------------------- */

static HWND mk_static(HWND parent, const wchar_t *text, int x, int y, int w, int h, HINSTANCE inst) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                           x, y, w, h, parent, NULL, inst, NULL);
}

static void create_controls(App *a) {
    HINSTANCE inst = a->inst;
    HWND p = a->main_wnd;
    int x0 = 18, y = 18;

    mk_static(p, L"Audio device:", x0, y, 140, 18, inst);
    a->cb_device = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        x0, y + 20, 440, 200, p, (HMENU)(INT_PTR)ID_CB_DEVICE, inst, NULL);
    y += 56;

    mk_static(p, L"Sensitivity  (lower = more sensitive)", x0, y, 260, 18, inst);
    a->lbl_sens = mk_static(p, L"1.0x", x0 + 380, y, 60, 18, inst);
    a->sl_sens = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
        x0, y + 20, 440, 30, p, (HMENU)(INT_PTR)ID_SL_SENS, inst, NULL);
    SendMessageW(a->sl_sens, TBM_SETRANGE, TRUE, MAKELPARAM(5, 20));
    SendMessageW(a->sl_sens, TBM_SETTICFREQ, 1, 0);
    SendMessageW(a->sl_sens, TBM_SETPOS, TRUE, 10);
    y += 60;

    mk_static(p, L"Overlay size (px)", x0, y, 260, 18, inst);
    a->lbl_size = mk_static(p, L"320 px", x0 + 380, y, 60, 18, inst);
    a->sl_size = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
        x0, y + 20, 440, 30, p, (HMENU)(INT_PTR)ID_SL_SIZE, inst, NULL);
    SendMessageW(a->sl_size, TBM_SETRANGE, TRUE, MAKELPARAM(200, 600));
    SendMessageW(a->sl_size, TBM_SETTICFREQ, 50, 0);
    SendMessageW(a->sl_size, TBM_SETPOS, TRUE, 320);
    y += 60;

    mk_static(p, L"Overlay position:", x0, y, 140, 18, inst);
    a->cb_pos = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        x0, y + 20, 220, 160, p, (HMENU)(INT_PTR)ID_CB_POS, inst, NULL);
    SendMessageW(a->cb_pos, CB_ADDSTRING, 0, (LPARAM)L"Top right");
    SendMessageW(a->cb_pos, CB_ADDSTRING, 0, (LPARAM)L"Top left");
    SendMessageW(a->cb_pos, CB_ADDSTRING, 0, (LPARAM)L"Bottom right");
    SendMessageW(a->cb_pos, CB_ADDSTRING, 0, (LPARAM)L"Bottom left");
    SendMessageW(a->cb_pos, CB_ADDSTRING, 0, (LPARAM)L"Center");
    SendMessageW(a->cb_pos, CB_SETCURSEL, 0, 0);

    a->ck_show = CreateWindowExW(0, L"BUTTON", L"Show overlay",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x0 + 250, y + 22, 180, 22, p, (HMENU)(INT_PTR)ID_CK_SHOW, inst, NULL);
    SendMessageW(a->ck_show, BM_SETCHECK, BST_CHECKED, 0);
    y += 60;

    a->btn_start = CreateWindowExW(0, L"BUTTON", L"Start",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        x0, y, 120, 34, p, (HMENU)(INT_PTR)ID_BTN_START, inst, NULL);
    a->btn_stop = CreateWindowExW(0, L"BUTTON", L"Stop",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x0 + 130, y, 120, 34, p, (HMENU)(INT_PTR)ID_BTN_STOP, inst, NULL);
    EnableWindow(a->btn_stop, FALSE);
    y += 48;

    a->lbl_status = mk_static(p, L"Ready. Select a device and click Start.",
                              x0, y, 440, 40, inst);

    populate_devices(a);
    update_slider_labels(a);
}

static void on_command(App *a, WPARAM wp) {
    WORD id   = LOWORD(wp);
    WORD code = HIWORD(wp);

    if (id == ID_BTN_START && code == BN_CLICKED) {
        start_pipeline(a);
    } else if (id == ID_BTN_STOP && code == BN_CLICKED) {
        stop_pipeline(a);
    } else if (id == ID_CK_SHOW && code == BN_CLICKED) {
        if (a->overlay) {
            if (IsDlgButtonChecked(a->main_wnd, ID_CK_SHOW) == BST_CHECKED) {
                overlay_show(a->overlay);
            } else {
                overlay_hide(a->overlay);
            }
        }
    } else if (id == ID_CB_POS && code == CBN_SELCHANGE) {
        if (a->overlay) overlay_reconfigure(a->overlay, slider_size(a), selected_position(a));
    }
}

static void on_hscroll(App *a, HWND from) {
    update_slider_labels(a);
    if (from == a->sl_sens && a->detector_ready) {
        detector_set_sensitivity(&a->detector, slider_sensitivity(a));
    } else if (from == a->sl_size && a->overlay) {
        overlay_reconfigure(a->overlay, slider_size(a), selected_position(a));
    }
}

static LRESULT CALLBACK main_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App *a = &g_app;
    switch (msg) {
        case WM_COMMAND:
            on_command(a, wp);
            return 0;
        case WM_HSCROLL:
            on_hscroll(a, (HWND)lp);
            return 0;
        case WM_HOTKEY:
            if ((int)wp == a->hotkey_id) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        case WM_CLOSE:
            stop_pipeline(a);
            if (a->overlay) { overlay_destroy(a->overlay); a->overlay = NULL; }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (a->hotkey_id) UnregisterHotKey(hwnd, a->hotkey_id);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void register_main_class(HINSTANCE inst) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = main_wndproc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = APP_CLASS;
    RegisterClassExW(&wc);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show) {
    (void)prev; (void)cmdline;
    memset(&g_app, 0, sizeof(g_app));
    g_app.inst = inst;

    INITCOMMONCONTROLSEX icc = { sizeof(icc),
        ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    register_main_class(inst);

    /* Size client area to fit controls. */
    RECT target = { 0, 0, 480, 360 };
    AdjustWindowRect(&target, WS_OVERLAPPEDWINDOW, FALSE);

    g_app.main_wnd = CreateWindowExW(0, APP_CLASS, APP_TITLE,
        WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME),
        CW_USEDEFAULT, CW_USEDEFAULT,
        target.right - target.left, target.bottom - target.top,
        NULL, NULL, inst, NULL);
    if (!g_app.main_wnd) return 1;

    create_controls(&g_app);

    /* Ctrl+F10 global hotkey to close the app. */
    g_app.hotkey_id = 1;
    RegisterHotKey(g_app.main_wnd, g_app.hotkey_id, MOD_CONTROL | MOD_NOREPEAT, VK_F10);

    ShowWindow(g_app.main_wnd, show);
    UpdateWindow(g_app.main_wnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(g_app.main_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
