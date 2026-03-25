#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum GameMode {
    MODE_SINGLEPLAYER = 0,
    MODE_COOP = 1,
    MODE_DEATHMATCH = 2,
    MODE_BROWSER = 3,
    MODE_NONE = -1
};

static const char *mode_name(enum GameMode mode)
{
    switch (mode)
    {
    case MODE_SINGLEPLAYER:
        return "Singleplayer";
    case MODE_COOP:
        return "Co-op";
    case MODE_DEATHMATCH:
        return "Deathmatch";
    case MODE_BROWSER:
        return "Lobby Browser";
    default:
        return "Unknown";
    }
}

static void launcher_message(const char *title, const char *text)
{
    MessageBoxA(NULL, text, title, MB_OK | MB_ICONINFORMATION);
}

static void launcher_error(const char *text)
{
    MessageBoxA(NULL, text, "DOOM Launcher - Error", MB_OK | MB_ICONERROR);
}

static int launcher_get_dir(char *path, size_t path_size)
{
    DWORD len = GetModuleFileNameA(NULL, path, (DWORD)path_size);
    int i;

    if (!len || len >= path_size)
    {
        path[0] = '\0';
        return 0;
    }

    for (i = (int)len - 1; i >= 0; --i)
    {
        if (path[i] == '\\' || path[i] == '/')
        {
            path[i] = '\0';
            return 1;
        }
    }

    path[0] = '\0';
    return 0;
}

static enum GameMode get_mode_from_cmdline(const char *cmd_line)
{
    if (!cmd_line || !cmd_line[0])
        return MODE_NONE;

    if (strstr(cmd_line, "-singleplayer"))
        return MODE_SINGLEPLAYER;
    if (strstr(cmd_line, "-coop"))
        return MODE_COOP;
    if (strstr(cmd_line, "-deathmatch") || strstr(cmd_line, "-altdeath"))
        return MODE_DEATHMATCH;
    if (strstr(cmd_line, "-browse") || strstr(cmd_line, "-steambrowser"))
        return MODE_BROWSER;

    return MODE_NONE;
}

static enum GameMode show_mode_dialog(void)
{
    int result;

    result = MessageBoxA(NULL,
                         "Select game mode:\n\n"
                         "Yes = Co-op Host\n"
                         "No = Deathmatch Host\n"
                         "Cancel = Lobby Browser\n\n"
                         "Use -singleplayer argument for solo launch.",
                         "DOOM Launcher - Game Mode",
                         MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);

    switch (result)
    {
    case IDYES:
        return MODE_COOP;
    case IDNO:
        return MODE_DEATHMATCH;
    case IDCANCEL:
        return MODE_BROWSER;
    default:
        return MODE_NONE;
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int cmd_show)
{
    char dir[MAX_PATH];
    char exe_path[MAX_PATH];
    char command[4096];
    const char *appid_env;
    const char *appid;
    enum GameMode mode;
    char mode_args[256];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    (void)instance;
    (void)prev_instance;
    (void)cmd_show;

    if (!launcher_get_dir(dir, sizeof(dir)))
    {
        launcher_error("Could not locate launcher directory.");
        return 1;
    }

    snprintf(exe_path, sizeof(exe_path), "%s\\doom.exe", dir);
    if (GetFileAttributesA(exe_path) == INVALID_FILE_ATTRIBUTES)
    {
        launcher_error("doom.exe was not found next to the launcher.");
        return 1;
    }

    mode = get_mode_from_cmdline(cmd_line);
    if (mode == MODE_NONE)
        mode = show_mode_dialog();

    if (mode == MODE_NONE)
    {
        launcher_error("No game mode selected. Exiting.");
        return 1;
    }

    appid_env = getenv("DOOM_STEAM_APPID");
    appid = (appid_env && appid_env[0]) ? appid_env : "480";

    mode_args[0] = '\0';
    switch (mode)
    {
    case MODE_SINGLEPLAYER:
        mode_args[0] = '\0';
        break;
    case MODE_COOP:
        snprintf(mode_args, sizeof(mode_args), " -net 1 -steamhost -steamallowsolo -steamappid %s", appid);
        break;
    case MODE_DEATHMATCH:
        snprintf(mode_args, sizeof(mode_args), " -deathmatch -net 1 -steamhost -steamallowsolo -steamappid %s", appid);
        break;
    case MODE_BROWSER:
        snprintf(mode_args, sizeof(mode_args), " -net 1 -steam -steamallowsolo -steambrowser -steamappid %s", appid);
        break;
    default:
        break;
    }

    snprintf(command, sizeof(command), "\"%s\"%s", exe_path, mode_args);

    if (cmd_line && cmd_line[0])
    {
        strncat(command, " ", sizeof(command) - strlen(command) - 1);
        strncat(command, cmd_line, sizeof(command) - strlen(command) - 1);
    }

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    if (!CreateProcessA(exe_path, command, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi))
    {
        launcher_error("Failed to launch doom.exe.");
        return 1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
