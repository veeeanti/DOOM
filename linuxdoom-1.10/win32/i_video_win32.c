#define boolean windows_boolean_workaround
#include <windows.h>
#include <windowsx.h>
#undef boolean

#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_system.h"
#include "i_video.h"
#include "m_argv.h"
#include "v_video.h"

static HWND s_hwnd;
static HDC s_hdc;
static BITMAPINFO s_bmi;
static unsigned int s_palette[256];
static unsigned int s_pixels[SCREENWIDTH * SCREENHEIGHT];
static int s_multiply = 2;
static int s_initialized;

static int translate_key(WPARAM vk)
{
    if (vk >= 'A' && vk <= 'Z')
        return (int)(vk - 'A' + 'a');
    if (vk >= '0' && vk <= '9')
        return (int)vk;

    switch (vk)
    {
    case VK_LEFT: return KEY_LEFTARROW;
    case VK_RIGHT: return KEY_RIGHTARROW;
    case VK_UP: return KEY_UPARROW;
    case VK_DOWN: return KEY_DOWNARROW;
    case VK_ESCAPE: return KEY_ESCAPE;
    case VK_RETURN: return KEY_ENTER;
    case VK_TAB: return KEY_TAB;
    case VK_F1: return KEY_F1;
    case VK_F2: return KEY_F2;
    case VK_F3: return KEY_F3;
    case VK_F4: return KEY_F4;
    case VK_F5: return KEY_F5;
    case VK_F6: return KEY_F6;
    case VK_F7: return KEY_F7;
    case VK_F8: return KEY_F8;
    case VK_F9: return KEY_F9;
    case VK_F10: return KEY_F10;
    case VK_F11: return KEY_F11;
    case VK_F12: return KEY_F12;
    case VK_BACK: return KEY_BACKSPACE;
    case VK_PAUSE: return KEY_PAUSE;
    case VK_OEM_PLUS: return KEY_EQUALS;
    case VK_OEM_MINUS: return KEY_MINUS;
    case VK_SHIFT: return KEY_RSHIFT;
    case VK_RSHIFT: return KEY_RSHIFT;
    case VK_LSHIFT: return KEY_RSHIFT;
    case VK_CONTROL: return KEY_RCTRL;
    case VK_RCONTROL: return KEY_RCTRL;
    case VK_LCONTROL: return KEY_RCTRL;
    case VK_MENU: return KEY_RALT;
    case VK_RMENU: return KEY_RALT;
    case VK_LMENU: return KEY_RALT;
    default:
        break;
    }

    if (vk >= 0x20 && vk <= 0x7e)
        return (int)vk;

    return 0;
}

static int current_mouse_buttons(void)
{
    int buttons = 0;
    if (GetKeyState(VK_LBUTTON) & 0x8000)
        buttons |= 1;
    if (GetKeyState(VK_MBUTTON) & 0x8000)
        buttons |= 2;
    if (GetKeyState(VK_RBUTTON) & 0x8000)
        buttons |= 4;
    return buttons;
}

static void post_mouse_event(int buttons, int dx, int dy)
{
    event_t event;
    event.type = ev_mouse;
    event.data1 = buttons;
    event.data2 = dx;
    event.data3 = dy;
    D_PostEvent(&event);
}

static LRESULT CALLBACK DoomWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    static int last_x;
    static int last_y;

    (void)hwnd;

    switch (msg)
    {
    case WM_CLOSE:
        I_Quit();
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (!(lparam & (1 << 30)))
        {
            event_t event;
            event.type = ev_keydown;
            event.data1 = translate_key(wparam);
            event.data2 = 0;
            event.data3 = 0;
            if (event.data1)
                D_PostEvent(&event);
        }
        return 0;

    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        event_t event;
        event.type = ev_keyup;
        event.data1 = translate_key(wparam);
        event.data2 = 0;
        event.data3 = 0;
        if (event.data1)
            D_PostEvent(&event);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lparam);
        int y = GET_Y_LPARAM(lparam);
        int dx = x - last_x;
        int dy = y - last_y;
        last_x = x;
        last_y = y;
        if (dx || dy)
            post_mouse_event(current_mouse_buttons(), dx << 2, -dy << 2);
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        post_mouse_event(current_mouse_buttons(), 0, 0);
        return 0;

    default:
        break;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

void I_StartFrame(void)
{
}

void I_StartTic(void)
{
    MSG msg;

    if (!s_initialized)
        return;

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
    int i;
    static int lasttic;
    RECT rect;
    int dst_w;
    int dst_h;

    if (!s_initialized)
        return;

    if (devparm)
    {
        int now = I_GetTime();
        int tics = now - lasttic;
        if (tics > 20)
            tics = 20;
        lasttic = now;

        for (i = 0; i < tics * 2; i += 2)
            screens[0][(SCREENHEIGHT - 1) * SCREENWIDTH + i] = 0xff;
        for (; i < 20 * 2; i += 2)
            screens[0][(SCREENHEIGHT - 1) * SCREENWIDTH + i] = 0x0;
    }

    for (i = 0; i < SCREENWIDTH * SCREENHEIGHT; ++i)
        s_pixels[i] = s_palette[screens[0][i]];

    GetClientRect(s_hwnd, &rect);
    dst_w = rect.right - rect.left;
    dst_h = rect.bottom - rect.top;

    StretchDIBits(s_hdc,
                  0,
                  0,
                  dst_w,
                  dst_h,
                  0,
                  0,
                  SCREENWIDTH,
                  SCREENHEIGHT,
                  s_pixels,
                  &s_bmi,
                  DIB_RGB_COLORS,
                  SRCCOPY);
}

void I_ReadScreen(byte *scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

void I_SetPalette(byte *palette)
{
    int i;
    for (i = 0; i < 256; ++i)
    {
        int r = gammatable[usegamma][*palette++];
        int g = gammatable[usegamma][*palette++];
        int b = gammatable[usegamma][*palette++];
        s_palette[i] = ((unsigned int)b << 16) | ((unsigned int)g << 8) | (unsigned int)r;
    }
}

void I_ShutdownGraphics(void)
{
    if (s_hdc)
    {
        ReleaseDC(s_hwnd, s_hdc);
        s_hdc = NULL;
    }

    if (s_hwnd)
    {
        DestroyWindow(s_hwnd);
        s_hwnd = NULL;
    }

    s_initialized = 0;
}

void I_InitGraphics(void)
{
    static int firsttime = 1;
    WNDCLASS wc;
    DWORD style;
    RECT rect;
    int window_w;
    int window_h;

    if (!firsttime)
        return;
    firsttime = 0;

    if (M_CheckParm("-1"))
        s_multiply = 1;
    if (M_CheckParm("-2"))
        s_multiply = 2;
    if (M_CheckParm("-3"))
        s_multiply = 3;
    if (M_CheckParm("-4"))
        s_multiply = 4;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = DoomWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "WinDoomClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClass(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        I_Error("RegisterClass failed");

    style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    window_w = SCREENWIDTH * s_multiply;
    window_h = SCREENHEIGHT * s_multiply;
    rect.left = 0;
    rect.top = 0;
    rect.right = window_w;
    rect.bottom = window_h;
    AdjustWindowRect(&rect, style, FALSE);

    s_hwnd = CreateWindow("WinDoomClass",
                          "DOOM",
                          style,
                          CW_USEDEFAULT,
                          CW_USEDEFAULT,
                          rect.right - rect.left,
                          rect.bottom - rect.top,
                          NULL,
                          NULL,
                          GetModuleHandle(NULL),
                          NULL);
    if (!s_hwnd)
        I_Error("CreateWindow failed");

    ShowWindow(s_hwnd, SW_SHOW);
    UpdateWindow(s_hwnd);

    s_hdc = GetDC(s_hwnd);
    if (!s_hdc)
        I_Error("GetDC failed");

    memset(&s_bmi, 0, sizeof(s_bmi));
    s_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    s_bmi.bmiHeader.biWidth = SCREENWIDTH;
    s_bmi.bmiHeader.biHeight = -SCREENHEIGHT;
    s_bmi.bmiHeader.biPlanes = 1;
    s_bmi.bmiHeader.biBitCount = 32;
    s_bmi.bmiHeader.biCompression = BI_RGB;

    if (M_CheckParm("-grabmouse"))
    {
        RECT clip;
        GetClientRect(s_hwnd, &clip);
        MapWindowPoints(s_hwnd, NULL, (POINT *)&clip, 2);
        ClipCursor(&clip);
    }

    s_initialized = 1;
}
