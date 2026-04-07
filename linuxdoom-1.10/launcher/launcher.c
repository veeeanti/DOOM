#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

#define MAX_WADS 256
#define MAX_PATH_LEN 1024

typedef struct {
    char name[MAX_PATH_LEN];
    char fullpath[MAX_PATH_LEN];
    DWORD size;
} WadInfo;

static WadInfo g_wads[MAX_WADS];
static int g_wad_count = 0;
static char g_wads_dir[MAX_PATH_LEN];

void trim_extension(char *out, const char *in, int out_size)
{
    const char *dot = strrchr(in, '.');
    if (dot)
    {
        int len = (int)(dot - in);
        if (len >= out_size) len = out_size - 1;
        strncpy(out, in, len);
        out[len] = '\0';
    }
    else
    {
        strncpy(out, in, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

int scan_wads(const char *dir)
{
    WIN32_FIND_DATA fd;
    HANDLE hFind;
    char search[MAX_PATH_LEN];
    char exe_dir[MAX_PATH_LEN];
    int count = 0;

    GetModuleFileNameA(NULL, exe_dir, MAX_PATH_LEN);
    char *last_slash = strrchr(exe_dir, '\\');
    if (last_slash) *last_slash = '\0';

    snprintf(g_wads_dir, MAX_PATH_LEN, "%s\\%s", exe_dir, dir);
    snprintf(search, MAX_PATH_LEN, "%s\\*.wad", g_wads_dir);

    hFind = FindFirstFileA(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        snprintf(g_wads_dir, MAX_PATH_LEN, "%s", exe_dir);
        snprintf(search, MAX_PATH_LEN, "%s\\*.wad", exe_dir);
        hFind = FindFirstFileA(search, &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            return 0;
    }

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            if (count < MAX_WADS)
            {
                snprintf(g_wads[count].fullpath, MAX_PATH_LEN, "%s\\%s", g_wads_dir, fd.nFileSizeLow);
                g_wads[count].size = fd.nFileSizeLow;
                trim_extension(g_wads[count].name, fd.cFileName, MAX_PATH_LEN);
                count++;
            }
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);

    for (int i = 0; i < count - 1; i++)
    {
        for (int j = i + 1; j < count; j++)
        {
            if (strcasecmp(g_wads[i].name, g_wads[j].name) > 0)
            {
                WadInfo temp = g_wads[i];
                g_wads[i] = g_wads[j];
                g_wads[j] = temp;
            }
        }
    }

    return count;
}

void draw_menu(HDC hdc, int selection, int count)
{
    HFONT hFont = CreateFontA(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
    HFONT hFontSmall = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");

    SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);

    SetTextColor(hdc, RGB(0, 0, 0));
    Rectangle(hdc, 0, 0, 600, 500);
    FillRect(hdc, &(RECT){0, 0, 600, 500}, CreateSolidBrush(RGB(20, 20, 30)));

    SetTextColor(hdc, RGB(180, 0, 0));
    TextOutA(hdc, 20, 20, "DOOM'93", 7);

    SetTextColor(hdc, RGB(200, 200, 200));
    SelectObject(hdc, hFontSmall);
    TextOutA(hdc, 20, 60, "Select a WAD to play:", 23);

    int start_y = 100;
    int item_height = 40;

    for (int i = 0; i < count; i++)
    {
        int y = start_y + i * item_height;
        if (i == selection)
        {
            SetTextColor(hdc, RGB(255, 50, 50));
            Rectangle(hdc, 10, y - 5, 590, y + item_height - 5);
        }
        else
        {
            SetTextColor(hdc, RGB(150, 150, 150));
        }
        TextOutA(hdc, 30, y, g_wads[i].name, (int)strlen(g_wads[i].name));
    }

    SetTextColor(hdc, RGB(100, 100, 100));
    TextOutA(hdc, 20, 460, "Arrow keys to navigate, Enter to select, Esc to exit", 58);

    DeleteObject(hFont);
    DeleteObject(hFontSmall);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    static int selection = 0;
    static int wad_count = 0;
    static int initialized = 0;

    switch (msg)
    {
    case WM_CREATE:
        wad_count = scan_wads("wads");
        if (wad_count == 0)
            wad_count = scan_wads(".");
        if (wad_count > 0) selection = 0;
        initialized = 1;
        SetTimer(hwnd, 1, 16, NULL);
        break;

    case WM_TIMER:
        if (initialized)
        {
            HDC hdc = GetDC(hwnd);
            draw_menu(hdc, selection, wad_count);
            ReleaseDC(hwnd, hdc);
        }
        break;

    case WM_KEYDOWN:
        if (wparam == VK_UP && selection > 0)
            selection--;
        else if (wparam == VK_DOWN && selection < wad_count - 1)
            selection++;
        else if (wparam == VK_RETURN && wad_count > 0)
        {
            char cmd[MAX_PATH_LEN + 32];
            STARTUPINFOA si = {0};
            PROCESS_INFORMATION pi = {0};
            si.cb = sizeof(si);

            snprintf(cmd, sizeof(cmd), "doom.exe -waddir \"%s\"", g_wads_dir);
            CreateProcessA("doom.exe", cmd, NULL, NULL, FALSE,
                CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
        else if (wparam == VK_ESCAPE)
        {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSA wc = {0};
    HWND hwnd;
    MSG msg;

    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "Doom93Launcher";
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)32512);
    RegisterClassA(&wc);

    hwnd = CreateWindowExA(0, "Doom93Launcher", "DOOM'93 Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 500,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    while (GetMessageA(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
