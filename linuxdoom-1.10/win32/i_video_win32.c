//----------------------------------------------------------
//            DOOM'93 Win32 video renderer
//
//  Clean high-quality video renderer with modern scaling modes
//  Supports integer scaling, smooth scaling, and sharp nearest
//
//  veeλnti is responsible for this
//----------------------------------------------------------

#define boolean windows_boolean_workaround
#include <windows.h>
#include <windowsx.h>
#undef boolean

#include <string.h>
#include <stdlib.h>

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
static unsigned int s_pixels[320 * 200];
static unsigned int *s_scalebuf = NULL;
static int s_scalebuf_width;
static int s_scalebuf_height;
static int s_initialized;
static int s_grabmouse;
static int s_rawinput;
static int s_mousecaptured;
static int s_cursorhidden;
static int s_ignore_mousemove;
static int s_fullscreen;
static RECT s_windowed_rect;
static DWORD s_windowed_style;
static int s_multiply;
static int s_smooth;
static int s_integer_scale;

static void set_mouse_capture(HWND hwnd, int capture);

static int translate_key(WPARAM vk)
{
    if (vk >= 'A' && vk <= 'Z')
        return (int)(vk - 'A' + 'a');
    if (vk >= '0' && vk <= '9')
        return (int)vk;

    switch (vk)
    {
    case VK_SPACE: return ' ';
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
    case VK_NUMPAD0: return '0';
    case VK_NUMPAD1: return '1';
    case VK_NUMPAD2: return '2';
    case VK_NUMPAD3: return '3';
    case VK_NUMPAD4: return '4';
    case VK_NUMPAD5: return '5';
    case VK_NUMPAD6: return '6';
    case VK_NUMPAD7: return '7';
    case VK_NUMPAD8: return '8';
    case VK_NUMPAD9: return '9';
    case VK_MULTIPLY: return '*';
    case VK_ADD: return '+';
    case VK_SUBTRACT: return KEY_MINUS;
    case VK_DECIMAL: return '.';
    case VK_DIVIDE: return '/';
    case VK_OEM_PLUS: return KEY_EQUALS;
    case VK_OEM_MINUS: return KEY_MINUS;
    case VK_OEM_1: return ';';
    case VK_OEM_2: return '/';
    case VK_OEM_3: return '`';
    case VK_OEM_4: return '[';
    case VK_OEM_5: return '\\';
    case VK_OEM_6: return ']';
    case VK_OEM_7: return '\'';
    case VK_OEM_COMMA: return ',';
    case VK_OEM_PERIOD: return '.';
    case VK_OEM_102: return '\\';
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

static void center_mouse_cursor(HWND hwnd, int clip)
{
    RECT rc;
    RECT clip_rc;
    POINT center;

    GetClientRect(hwnd, &rc);
    center.x = (rc.left + rc.right) / 2;
    center.y = (rc.top + rc.bottom) / 2;

    clip_rc = rc;
    MapWindowPoints(hwnd, NULL, (POINT *)&clip_rc, 2);
    ClientToScreen(hwnd, &center);

    if (clip)
        ClipCursor(&clip_rc);

    SetCursorPos(center.x, center.y);
    s_ignore_mousemove = 1;
}

static int should_grab_mouse(void)
{
    return s_grabmouse && gamestate == GS_LEVEL && !paused && !menuactive;
}

static void set_mouse_capture(HWND hwnd, int capture)
{
    if (!hwnd)
        return;

    if (capture)
    {
        RECT clip;
        POINT center;

        if (s_mousecaptured)
            return;

        if (!should_grab_mouse())
            return;

        SetCapture(hwnd);

        if (!s_cursorhidden)
        {
            while (ShowCursor(FALSE) >= 0)
                ;
            s_cursorhidden = 1;
        }

        GetClientRect(hwnd, &clip);
        center.x = (clip.left + clip.right) / 2;
        center.y = (clip.top + clip.bottom) / 2;

        MapWindowPoints(hwnd, NULL, (POINT *)&clip, 2);
        ClientToScreen(hwnd, &center);
        ClipCursor(&clip);
        SetCursorPos(center.x, center.y);
        s_ignore_mousemove = 1;

        s_mousecaptured = 1;
    }
    else
    {
        if (!s_mousecaptured)
            return;

        if (GetCapture() == hwnd)
            ReleaseCapture();

        ClipCursor(NULL);

        if (s_cursorhidden)
        {
            while (ShowCursor(TRUE) < 0)
                ;
            s_cursorhidden = 0;
        }

        s_mousecaptured = 0;
        s_ignore_mousemove = 0;
    }
}

static void apply_fullscreen_state(void)
{
    if (!s_hwnd)
        return;

    if (s_fullscreen)
    {
        MONITORINFO mi;

        s_windowed_style = (DWORD)GetWindowLong(s_hwnd, GWL_STYLE);
        GetWindowRect(s_hwnd, &s_windowed_rect);

        SetWindowLong(s_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);

        mi.cbSize = sizeof(mi);
        GetMonitorInfo(MonitorFromWindow(s_hwnd, MONITOR_DEFAULTTONEAREST), &mi);

        SetWindowPos(s_hwnd,
                     HWND_TOP,
                     mi.rcMonitor.left,
                     mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    else
    {
        SetWindowLong(s_hwnd, GWL_STYLE, s_windowed_style);
        SetWindowPos(s_hwnd,
                     NULL,
                     s_windowed_rect.left,
                     s_windowed_rect.top,
                     s_windowed_rect.right - s_windowed_rect.left,
                     s_windowed_rect.bottom - s_windowed_rect.top,
                     SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }

    if (s_grabmouse)
        set_mouse_capture(s_hwnd, 1);
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

    case WM_SETFOCUS:
        if (should_grab_mouse())
            set_mouse_capture(hwnd, 1);
        return 0;

    case WM_KILLFOCUS:
        set_mouse_capture(hwnd, 0);
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE)
            set_mouse_capture(hwnd, 0);
        else if (should_grab_mouse())
            set_mouse_capture(hwnd, 1);
        return 0;

    case WM_SETCURSOR:
        if (s_mousecaptured)
        {
            SetCursor(NULL);
            return TRUE;
        }
        break;

    case WM_INPUT:
        if (s_mousecaptured && s_rawinput)
        {
            UINT size = sizeof(RAWINPUT);
            RAWINPUT raw;

            if (GetRawInputData((HRAWINPUT)lparam,
                                RID_INPUT,
                                &raw,
                                &size,
                                sizeof(RAWINPUTHEADER)) == (UINT)-1)
                return 0;

            if (raw.header.dwType == RIM_TYPEMOUSE)
            {
                int dx = (int)raw.data.mouse.lLastX;
                int dy = (int)raw.data.mouse.lLastY;

                if (dx || dy)
                {
                    post_mouse_event(current_mouse_buttons(), dx << 2, -dy << 2);
                    center_mouse_cursor(hwnd, 1);
                }
            }
            return 0;
        }
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if ((wparam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))
            || wparam == VK_F11)
        {
            I_ToggleFullscreen();
            return 0;
        }

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
        if (s_mousecaptured && s_rawinput)
            return 0;

        if (s_ignore_mousemove)
        {
            s_ignore_mousemove = 0;
            return 0;
        }

        if (s_mousecaptured)
        {
            RECT rc;
            POINT center;
            int x = GET_X_LPARAM(lparam);
            int y = GET_Y_LPARAM(lparam);
            int dx;
            int dy;

            GetClientRect(hwnd, &rc);
            center.x = (rc.left + rc.right) / 2;
            center.y = (rc.top + rc.bottom) / 2;

            dx = x - center.x;
            dy = y - center.y;

            if (dx || dy)
                post_mouse_event(current_mouse_buttons(), dx << 2, -dy << 2);

            ClientToScreen(hwnd, &center);
            SetCursorPos(center.x, center.y);
            s_ignore_mousemove = 1;
            return 0;
        }

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
        if (s_grabmouse)
            set_mouse_capture(hwnd, 1);
        post_mouse_event(current_mouse_buttons(), 0, 0);
        return 0;

    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (s_grabmouse)
            set_mouse_capture(hwnd, 1);
        post_mouse_event(current_mouse_buttons(), 0, 0);
        return 0;

    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
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

    if (should_grab_mouse() && !s_mousecaptured)
        set_mouse_capture(s_hwnd, 1);
    else if (!should_grab_mouse() && s_mousecaptured)
        set_mouse_capture(s_hwnd, 0);

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void I_UpdateNoBlit(void)
{
}

static void ensure_scale_buffer(int width, int height)
{
    if (s_scalebuf && s_scalebuf_width == width && s_scalebuf_height == height)
        return;

    if (s_scalebuf)
    {
        free(s_scalebuf);
        s_scalebuf = NULL;
    }

    s_scalebuf = malloc((size_t)width * height * sizeof(unsigned int));
    s_scalebuf_width = width;
    s_scalebuf_height = height;
}

static void scale_pixels(int factor)
{
    int x, y, sx, sy;
    int out_w = SCREENWIDTH * factor;
    int out_h = SCREENHEIGHT * factor;

    ensure_scale_buffer(out_w, out_h);

    for (y = 0; y < out_h; y++)
    {
        sy = y / factor;
        for (x = 0; x < out_w; x++)
        {
            sx = x / factor;
            s_scalebuf[y * out_w + x] = s_pixels[sy * SCREENWIDTH + sx];
        }
    }
}

void I_FinishUpdate(void)
{
    int i;
    static int lasttic;
    RECT rect;
    int client_w;
    int client_h;
    int dst_x;
    int dst_y;
    int dst_w;
    int dst_h;
    int scale_factor;
    int scale_w;
    int scale_h;
    int mode;

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
    client_w = rect.right - rect.left;
    client_h = rect.bottom - rect.top;

    if (client_w <= 0 || client_h <= 0)
        return;

    dst_w = client_w;
    dst_h = client_h;

    // 4:3 aspect ratio correction
    if (dst_w * 3 > dst_h * 4)
    {
        int fit_w = dst_h * 4 / 3;
        dst_x = (dst_w - fit_w) / 2;
        dst_y = 0;
        dst_w = fit_w;
    }
    else
    {
        int fit_h = dst_w * 3 / 4;
        dst_x = 0;
        dst_y = (dst_h - fit_h) / 2;
        dst_h = fit_h;
    }

    // Black bars
    if (dst_x > 0)
        PatBlt(s_hdc, 0, 0, dst_x, client_h, BLACKNESS);
    if (dst_x + dst_w < client_w)
        PatBlt(s_hdc, dst_x + dst_w, 0, client_w - (dst_x + dst_w), client_h, BLACKNESS);
    if (dst_y > 0)
        PatBlt(s_hdc, 0, 0, client_w, dst_y, BLACKNESS);
    if (dst_y + dst_h < client_h)
        PatBlt(s_hdc, 0, dst_y + dst_h, client_w, client_h - (dst_y + dst_h), BLACKNESS);

    // Calculate best scale factor
    scale_factor = dst_w / SCREENWIDTH;
    if (dst_h / SCREENHEIGHT < scale_factor)
        scale_factor = dst_h / SCREENHEIGHT;
    if (scale_factor < 1)
        scale_factor = 1;

    scale_w = SCREENWIDTH * scale_factor;
    scale_h = SCREENHEIGHT * scale_factor;

    if (s_integer_scale)
    {
        // Integer scaling - sharp pixel-perfect
        scale_pixels(scale_factor);

        dst_x += (dst_w - scale_w) / 2;
        dst_y += (dst_h - scale_h) / 2;
        dst_w = scale_w;
        dst_h = scale_h;

        mode = COLORONCOLOR;
    }
    else if (s_smooth)
    {
        // Smooth scaling - clean bilinear
        mode = HALFTONE;
    }
    else
    {
        // Nearest neighbor - classic pixel look
        mode = COLORONCOLOR;
    }

    SetStretchBltMode(s_hdc, mode);
    if (mode == HALFTONE)
        SetBrushOrgEx(s_hdc, 0, 0, NULL);

    s_bmi.bmiHeader.biWidth = s_integer_scale ? s_scalebuf_width : SCREENWIDTH;
    s_bmi.bmiHeader.biHeight = -(s_integer_scale ? s_scalebuf_height : SCREENHEIGHT);

    StretchDIBits(s_hdc,
                  dst_x,
                  dst_y,
                  dst_w,
                  dst_h,
                  0,
                  0,
                  s_integer_scale ? s_scalebuf_width : SCREENWIDTH,
                  s_integer_scale ? s_scalebuf_height : SCREENHEIGHT,
                  s_integer_scale ? s_scalebuf : s_pixels,
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
        s_palette[i] = ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
    }
}

void I_ShutdownGraphics(void)
{
    set_mouse_capture(s_hwnd, 0);

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

    if (s_scalebuf)
    {
        free(s_scalebuf);
        s_scalebuf = NULL;
    }

    s_initialized = 0;
}

void I_ToggleFullscreen(void)
{
    s_fullscreen = !s_fullscreen;
    apply_fullscreen_state();
}

int I_IsFullscreen(void)
{
    return s_fullscreen;
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

    // Single-instance check using a named mutex
    {
        HANDLE hMutex = CreateMutex(NULL, TRUE, "WinDoom_SingleInstance_Mutex");
        if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            // Another instance is already running, exit silently
            exit(0);
        }
    }

    if (M_CheckParm("-1"))
        s_multiply = 1;
    if (M_CheckParm("-2"))
        s_multiply = 2;
    if (M_CheckParm("-3"))
        s_multiply = 3;
    if (M_CheckParm("-4"))
        s_multiply = 4;
    if (M_CheckParm("-5"))
        s_multiply = 5;
    if (M_CheckParm("-6"))
        s_multiply = 6;

    s_smooth = M_CheckParm("-smooth") || !M_CheckParm("-sharp");
    s_integer_scale = M_CheckParm("-integerscale") ? 1 : 0;
    s_multiply = 4; // Default value

    if (!M_CheckParm("-1")
        && !M_CheckParm("-2")
        && !M_CheckParm("-3")
        && !M_CheckParm("-4")
        && !M_CheckParm("-5")
        && !M_CheckParm("-6"))
    {
        RECT work;
        int work_w;
        int work_h;
        int fit_w;
        int fit_h;
        int auto_mul;

        work.left = 0;
        work.top = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
        SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);

        work_w = work.right - work.left;
        work_h = work.bottom - work.top;

        fit_w = (work_w * 9) / 10;
        fit_h = (work_h * 9) / 10;

        auto_mul = fit_w / SCREENWIDTH;
        if (fit_h / ((SCREENHEIGHT * 6) / 5) < auto_mul)
            auto_mul = fit_h / ((SCREENHEIGHT * 6) / 5);

        if (auto_mul < 2)
            auto_mul = 2;
        if (auto_mul > 8)
            auto_mul = 8;

        s_multiply = auto_mul;
    }

    s_fullscreen = M_CheckParm("-fullscreen") && !M_CheckParm("-windowed");

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = DoomWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "WinDoomClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClass(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        I_Error("RegisterClass failed");

    style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    window_w = 320 * s_multiply;
    window_h = (200 * 6 / 5) * s_multiply;
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

    s_windowed_style = style;
    GetWindowRect(s_hwnd, &s_windowed_rect);

    if (s_fullscreen)
        apply_fullscreen_state();

    s_hdc = GetDC(s_hwnd);
    if (!s_hdc)
        I_Error("GetDC failed");

    memset(&s_bmi, 0, sizeof(s_bmi));
    s_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    s_bmi.bmiHeader.biWidth = 320;
    s_bmi.bmiHeader.biHeight = -200;
    s_bmi.bmiHeader.biPlanes = 1;
    s_bmi.bmiHeader.biBitCount = 32;
    s_bmi.bmiHeader.biCompression = BI_RGB;

    {
        RAWINPUTDEVICE raw_device;

        raw_device.usUsagePage = 0x01;
        raw_device.usUsage = 0x02;
        raw_device.dwFlags = 0;
        raw_device.hwndTarget = s_hwnd;
        s_rawinput = RegisterRawInputDevices(&raw_device, 1, sizeof(raw_device)) ? 1 : 0;
    }

    s_grabmouse = !M_CheckParm("-nograbmouse") || M_CheckParm("-grabmouse");

    s_initialized = 1;
}