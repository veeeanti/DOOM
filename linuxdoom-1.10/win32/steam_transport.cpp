//----------------------------------------------------------
//            DOOM'93 Steamworks implementation
//
//  This was the best idea I could come up with that I
//  knew I could work with easily and (hopefully) get
//  a viable replacement for the DOS networking code
//  left to us by id Software. Steam has a testing app
//  id (480, Spacewar) that we can use, everyone has it!
//  Lobbies are still broken as far as I know. It is
//  REALLY starting to piss me off too. lmao.
//
//  copyright Valve Software, written here by veeλnti
//----------------------------------------------------------

#include "win32/steam_transport.h"

extern "C" {
#include "m_argv.h"
#include "doomstat.h"
#include "win32/discord_rpc_win32.h"
}

#include <algorithm>
#include <cstdint>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#ifdef MAXINT
#undef MAXINT
#endif
#ifdef MININT
#undef MININT
#endif
#ifdef MINCHAR
#undef MINCHAR
#endif
#ifdef MAXCHAR
#undef MAXCHAR
#endif
#ifdef MINSHORT
#undef MINSHORT
#endif
#ifdef MAXSHORT
#undef MAXSHORT
#endif
#ifdef MINLONG
#undef MINLONG
#endif
#ifdef MAXLONG
#undef MAXLONG
#endif

#define boolean windows_boolean_workaround
#include <windows.h>
#undef boolean

static int g_active;
static char g_last_error[256] = "Steam transport inactive";
static int g_requested_appid = 480;
static int g_enable_restart_check;
static doomcom_t *g_doomcom;

static void steam_log(const char *msg);
static void steam_logf(const char *fmt, ...);
static int has_switch(int argc, char **argv, const char *name);
static const char *switch_value(int argc, char **argv, const char *name);

static unsigned long long steam_lobby_id_from_secret(const char *secret)
{
    if (!secret || !secret[0])
        return 0;

    if (!_strnicmp(secret, "steam:", 6))
        return _strtoui64(secret + 6, NULL, 10);

    return _strtoui64(secret, NULL, 10);
}

#ifdef DOOM_ENABLE_STEAMWORKS
#include "steam/steam_api.h"

#define STEAM_BROWSER_MAX_LOBBIES 16
#define STEAM_MAX_PLAYERS 4

typedef struct
{
    unsigned long long lobby_id;
    char title[96];
    char details[128];
    char mode[16];
    int members;
    int member_limit;
    int quickplay;
} steam_browser_lobby_t;

static int g_lobby_list_pending;
static int g_lobby_count;
static int g_host_mode;
static unsigned long long g_current_lobby_id;
static steam_browser_lobby_t g_browser_lobbies[STEAM_BROWSER_MAX_LOBBIES];
static CSteamID g_node_ids[MAXNETNODES];
static int g_node_count;
static int g_console_node;
static DWORD g_next_member_refresh_ms;

static void steam_reset_browser_results(void);
static int steam_refresh_node_map(void);
static int steam_wait_for_lobby(unsigned int timeout_ms);
static int steam_wait_for_min_members(int min_members, unsigned int timeout_ms);
static int steam_wait_for_lobby_list(unsigned int timeout_ms);
static const char *steam_normalize_mode(const char *mode);
static const char *steam_quick_mode_from_args(int argc, char **argv);
static int steam_try_quick_join(const char *quick_mode, unsigned long long *joined_lobby_id);

static const char *steam_mode_from_args(int argc, char **argv)
{
    const char *quick_mode = steam_quick_mode_from_args(argc, argv);

    if (quick_mode)
        return quick_mode;

    if (has_switch(argc, argv, "-altdeath") || has_switch(argc, argv, "-deathmatch"))
        return "deathmatch";
    return "coop";
}

static const char *steam_normalize_mode(const char *mode)
{
    if (!mode || !mode[0])
        return NULL;

    if (!_stricmp(mode, "deathmatch") || !_stricmp(mode, "dm") || !_stricmp(mode, "-deathmatch"))
        return "deathmatch";

    if (!_stricmp(mode, "coop") || !_stricmp(mode, "co-op") || !_stricmp(mode, "cooperative"))
        return "coop";

    return NULL;
}

static const char *steam_quick_mode_from_args(int argc, char **argv)
{
    const char *quick_mode = switch_value(argc, argv, "-steamquick");

    if (quick_mode)
        return steam_normalize_mode(quick_mode);
    if (has_switch(argc, argv, "-quickcoop"))
        return "coop";
    if (has_switch(argc, argv, "-quickdm"))
        return "deathmatch";

    return NULL;
}

static void steam_map_from_args(int argc, char **argv, char *out, size_t out_size)
{
    int i;

    snprintf(out, out_size, "MAP01");

    for (i = 1; i < argc; ++i)
    {
        if (_stricmp(argv[i], "-warp") != 0)
            continue;

        if (i + 2 < argc && argv[i + 2][0] != '-')
        {
            snprintf(out, out_size, "E%sM%s", argv[i + 1], argv[i + 2]);
            return;
        }

        if (i + 1 < argc)
        {
            int mapnum = atoi(argv[i + 1]);
            if (mapnum > 0)
                snprintf(out, out_size, "MAP%02d", mapnum);
        }
        return;
    }
}

static void steam_wad_from_args(int argc, char **argv, char *out, size_t out_size)
{
    int i;
    int first = 1;

    snprintf(out, out_size, "IWAD");

    for (i = 1; i < argc; ++i)
    {
        if (_stricmp(argv[i], "-file") != 0)
            continue;

        out[0] = '\0';
        while (i + 1 < argc && argv[i + 1][0] != '-')
        {
            const char *name;
            const char *slash;
            i++;
            name = argv[i];
            slash = strrchr(name, '/');
            if (!slash)
                slash = strrchr(name, '\\');
            name = slash ? slash + 1 : name;

            if (!first && strlen(out) + 1 < out_size)
                strncat(out, ",", out_size - strlen(out) - 1);
            if (strlen(out) + strlen(name) < out_size)
                strncat(out, name, out_size - strlen(out) - 1);
            first = 0;
        }

        if (out[0] == '\0')
            snprintf(out, out_size, "IWAD");
        return;
    }
}

static int steam_find_node_for_user(CSteamID user)
{
    int i;
    uint64 uid;

    if (!user.IsValid())
        return -1;

    uid = user.ConvertToUint64();
    for (i = 0; i < g_node_count; ++i)
    {
        if (g_node_ids[i].IsValid() && g_node_ids[i].ConvertToUint64() == uid)
            return i;
    }

    return -1;
}

static void steam_update_discord_presence(void)
{
    if (!g_current_lobby_id)
    {
        I_DiscordRPC_SetSteamJoin(0, 0, 0, NULL, NULL);
        return;
    }

    {
        CSteamID lobby(g_current_lobby_id);
        const char *mode = SteamMatchmaking()->GetLobbyData(lobby, "mode");
        const char *content = SteamMatchmaking()->GetLobbyData(lobby, "wad");
        int member_limit = SteamMatchmaking()->GetLobbyMemberLimit(lobby);

        if (member_limit <= 0)
            member_limit = STEAM_MAX_PLAYERS;

        I_DiscordRPC_SetSteamJoin(g_current_lobby_id,
                                  g_node_count > 0 ? g_node_count : 1,
                                  member_limit,
                                  mode,
                                  content);
    }
}

static void steam_reset_browser_results(void)
{
    memset(g_browser_lobbies, 0, sizeof(g_browser_lobbies));
    g_lobby_count = 0;
}

static void steam_update_lobby_metadata(CSteamID lobby)
{
    char map_name[32];
    char wad_name[96];
    char lobby_name[96];
    const char *persona;
    const char *mode;
    const char *quick_mode;

    if (!lobby.IsValid())
        return;

    mode = steam_mode_from_args(myargc, myargv);
    quick_mode = steam_quick_mode_from_args(myargc, myargv);
    steam_map_from_args(myargc, myargv, map_name, sizeof(map_name));
    steam_wad_from_args(myargc, myargv, wad_name, sizeof(wad_name));

    persona = SteamFriends() ? SteamFriends()->GetPersonaName() : "Host";
    if (quick_mode)
        snprintf(lobby_name, sizeof(lobby_name), "Quick %s", !_stricmp(quick_mode, "deathmatch") ? "Deathmatch" : "Co-op");
    else
        snprintf(lobby_name, sizeof(lobby_name), "%s's lobby", persona && persona[0] ? persona : "Host");

    SteamMatchmaking()->SetLobbyData(lobby, "game", "doom-steam");
    SteamMatchmaking()->SetLobbyData(lobby, "name", lobby_name);
    SteamMatchmaking()->SetLobbyData(lobby, "mode", mode);
    SteamMatchmaking()->SetLobbyData(lobby, "quickplay", quick_mode ? "1" : "0");
    SteamMatchmaking()->SetLobbyData(lobby, "map", map_name);
    SteamMatchmaking()->SetLobbyData(lobby, "wad", wad_name);
    SteamMatchmaking()->SetLobbyData(lobby, "version", "1.10-steamproto1");
    SteamMatchmaking()->SetLobbyData(lobby, "host", persona && persona[0] ? persona : "Host");
    SteamMatchmaking()->SetLobbyJoinable(lobby, true);
    SteamMatchmaking()->SetLobbyMemberLimit(lobby, STEAM_MAX_PLAYERS);
}

static int steam_refresh_node_map(void)
{
    CSteamID lobby;
    CSteamID owner;
    CSteamID self;
    std::vector<uint64> others;
    int members;
    int i;

    if (!g_current_lobby_id)
        return 0;

    lobby = CSteamID(g_current_lobby_id);
    if (!lobby.IsValid())
        return 0;

    members = SteamMatchmaking()->GetNumLobbyMembers(lobby);
    if (members < 1)
        members = 1;

    owner = SteamMatchmaking()->GetLobbyOwner(lobby);
    self = SteamUser()->GetSteamID();

    g_node_count = 0;
    memset(g_node_ids, 0, sizeof(g_node_ids));

    if (owner.IsValid() && g_node_count < MAXNETNODES)
        g_node_ids[g_node_count++] = owner;

    for (i = 0; i < members; ++i)
    {
        CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(lobby, i);
        if (!member.IsValid())
            continue;
        if (owner.IsValid() && member.ConvertToUint64() == owner.ConvertToUint64())
            continue;
        others.push_back(member.ConvertToUint64());
    }

    std::sort(others.begin(), others.end());
    for (i = 0; i < (int)others.size() && g_node_count < MAXNETNODES; ++i)
        g_node_ids[g_node_count++] = CSteamID(others[i]);

    if (steam_find_node_for_user(self) < 0 && g_node_count < MAXNETNODES)
        g_node_ids[g_node_count++] = self;

    if (g_node_count < 1)
        g_node_ids[g_node_count++] = self;

    g_console_node = steam_find_node_for_user(self);
    if (g_console_node < 0)
        g_console_node = 0;

    SteamNetworking()->AllowP2PPacketRelay(true);
    for (i = 0; i < g_node_count; ++i)
    {
        if (g_node_ids[i].IsValid() && g_node_ids[i].ConvertToUint64() != self.ConvertToUint64())
            SteamNetworking()->AcceptP2PSessionWithUser(g_node_ids[i]);
    }

    if (g_doomcom)
    {
        g_doomcom->numnodes = g_node_count;
        g_doomcom->numplayers = g_node_count;
        g_doomcom->consoleplayer = g_console_node;
    }

    netgame = g_current_lobby_id != 0;

    steam_update_discord_presence();
    return g_node_count;
}

static int steam_wait_for_lobby(unsigned int timeout_ms)
{
    DWORD end_time = GetTickCount() + timeout_ms;
    while (!g_current_lobby_id && GetTickCount() < end_time)
    {
        SteamAPI_RunCallbacks();
        Sleep(10);
    }

    return g_current_lobby_id != 0;
}

static int steam_wait_for_min_members(int min_members, unsigned int timeout_ms)
{
    DWORD end_time;

    if (min_members < 1)
        min_members = 1;

    end_time = GetTickCount() + timeout_ms;
    while (GetTickCount() < end_time)
    {
        SteamAPI_RunCallbacks();
        steam_refresh_node_map();
        if (g_node_count >= min_members)
            return 1;
        Sleep(25);
    }

    return g_node_count >= min_members;
}

static int steam_wait_for_lobby_list(unsigned int timeout_ms)
{
    DWORD end_time = GetTickCount() + timeout_ms;

    while (g_lobby_list_pending && GetTickCount() < end_time)
    {
        SteamAPI_RunCallbacks();
        Sleep(10);
    }

    return g_lobby_list_pending == 0;
}

class DoomSteamCallbacks
{
public:
    CCallResult<DoomSteamCallbacks, LobbyCreated_t> lobby_created_call;
    CCallResult<DoomSteamCallbacks, LobbyMatchList_t> lobby_list_call;
    CCallResult<DoomSteamCallbacks, LobbyEnter_t> lobby_enter_call;
    CCallback<DoomSteamCallbacks, LobbyChatUpdate_t, false> lobby_chat_update_cb;
    CCallback<DoomSteamCallbacks, LobbyDataUpdate_t, false> lobby_data_update_cb;
    CCallback<DoomSteamCallbacks, GameLobbyJoinRequested_t, false> lobby_join_request_cb;
    CCallback<DoomSteamCallbacks, P2PSessionRequest_t, false> p2p_request_cb;
    CCallback<DoomSteamCallbacks, P2PSessionConnectFail_t, false> p2p_fail_cb;

    DoomSteamCallbacks()
        : lobby_chat_update_cb(this, &DoomSteamCallbacks::OnLobbyChatUpdate)
        , lobby_data_update_cb(this, &DoomSteamCallbacks::OnLobbyDataUpdate)
        , lobby_join_request_cb(this, &DoomSteamCallbacks::OnLobbyJoinRequested)
        , p2p_request_cb(this, &DoomSteamCallbacks::OnP2PSessionRequest)
        , p2p_fail_cb(this, &DoomSteamCallbacks::OnP2PSessionConnectFail)
    {
    }

    void OnLobbyCreated(LobbyCreated_t *data, bool io_failure)
    {
        CSteamID lobby;

        if (io_failure || !data || data->m_eResult != k_EResultOK)
        {
            snprintf(g_last_error, sizeof(g_last_error), "Steam lobby creation failed");
            steam_log(g_last_error);
            return;
        }

        lobby = CSteamID(data->m_ulSteamIDLobby);
        g_current_lobby_id = lobby.ConvertToUint64();
        steam_update_lobby_metadata(lobby);
        steam_refresh_node_map();
        steam_logf("created public lobby %llu", g_current_lobby_id);
    }

    void OnLobbyMatchList(LobbyMatchList_t *data, bool io_failure)
    {
        int i;
        int count;

        g_lobby_list_pending = 0;
        steam_reset_browser_results();

        if (io_failure || !data)
        {
            snprintf(g_last_error, sizeof(g_last_error), "Steam lobby list request failed");
            steam_log(g_last_error);
            return;
        }

        count = (int)data->m_nLobbiesMatching;
        if (count > STEAM_BROWSER_MAX_LOBBIES)
            count = STEAM_BROWSER_MAX_LOBBIES;

        for (i = 0; i < count; ++i)
        {
            CSteamID lobby = SteamMatchmaking()->GetLobbyByIndex(i);
            const char *mode;
            const char *map;
            const char *wad;
            const char *ver;
            int members;
            int member_limit;

            if (!lobby.IsValid())
                continue;

            mode = SteamMatchmaking()->GetLobbyData(lobby, "mode");
            map = SteamMatchmaking()->GetLobbyData(lobby, "map");
            wad = SteamMatchmaking()->GetLobbyData(lobby, "wad");
            ver = SteamMatchmaking()->GetLobbyData(lobby, "version");
            const char *quickplay = SteamMatchmaking()->GetLobbyData(lobby, "quickplay");
            members = SteamMatchmaking()->GetNumLobbyMembers(lobby);
            member_limit = SteamMatchmaking()->GetLobbyMemberLimit(lobby);
            if (member_limit <= 0)
                member_limit = STEAM_MAX_PLAYERS;

            g_browser_lobbies[g_lobby_count].lobby_id = lobby.ConvertToUint64();
            g_browser_lobbies[g_lobby_count].members = members;
            g_browser_lobbies[g_lobby_count].member_limit = member_limit;
            g_browser_lobbies[g_lobby_count].quickplay = (quickplay && quickplay[0] == '1') ? 1 : 0;
            snprintf(g_browser_lobbies[g_lobby_count].mode,
                     sizeof(g_browser_lobbies[g_lobby_count].mode),
                     "%s",
                     mode && mode[0] ? mode : "coop");
            snprintf(g_browser_lobbies[g_lobby_count].title,
                     sizeof(g_browser_lobbies[g_lobby_count].title),
                     "%s%s %s %d/%d",
                     g_browser_lobbies[g_lobby_count].quickplay ? "[Q] " : "",
                     mode && mode[0] ? mode : "coop",
                     map && map[0] ? map : "MAP01",
                     members,
                     member_limit);
            snprintf(g_browser_lobbies[g_lobby_count].details,
                     sizeof(g_browser_lobbies[g_lobby_count].details),
                     "WAD:%s VER:%s ID:%llu",
                     wad && wad[0] ? wad : "IWAD",
                     ver && ver[0] ? ver : "1.10",
                     g_browser_lobbies[g_lobby_count].lobby_id);
            ++g_lobby_count;
        }

        steam_logf("lobby browser updated: %d visible lobbies", g_lobby_count);
    }

    void OnLobbyEnter(LobbyEnter_t *data, bool io_failure)
    {
        if (io_failure || !data)
        {
            snprintf(g_last_error, sizeof(g_last_error), "Steam lobby join failed");
            steam_log(g_last_error);
            return;
        }

        g_current_lobby_id = (unsigned long long)data->m_ulSteamIDLobby;
        steam_refresh_node_map();
        steam_logf("entered lobby %llu (%d nodes visible)", g_current_lobby_id, g_node_count);
    }

    void OnLobbyChatUpdate(LobbyChatUpdate_t *data)
    {
        if (!data)
            return;
        if ((unsigned long long)data->m_ulSteamIDLobby != g_current_lobby_id)
            return;
        steam_refresh_node_map();
    }

    void OnLobbyDataUpdate(LobbyDataUpdate_t *data)
    {
        if (!data)
            return;
        if ((unsigned long long)data->m_ulSteamIDLobby != g_current_lobby_id)
            return;
        steam_refresh_node_map();
    }

    void OnLobbyJoinRequested(GameLobbyJoinRequested_t *data)
    {
        SteamAPICall_t call;

        if (!data)
            return;

        call = SteamMatchmaking()->JoinLobby(data->m_steamIDLobby);
        lobby_enter_call.Set(call, this, &DoomSteamCallbacks::OnLobbyEnter);
        steam_logf("join requested via Steam for lobby %llu",
                   (unsigned long long)data->m_steamIDLobby.ConvertToUint64());
    }

    void OnP2PSessionRequest(P2PSessionRequest_t *data)
    {
        if (!data)
            return;
        SteamNetworking()->AcceptP2PSessionWithUser(data->m_steamIDRemote);
    }

    void OnP2PSessionConnectFail(P2PSessionConnectFail_t *data)
    {
        if (!data)
            return;
        steam_logf("p2p session failure with %llu (err=%d)",
                   (unsigned long long)data->m_steamIDRemote.ConvertToUint64(),
                   (int)data->m_eP2PSessionError);
    }
};

static DoomSteamCallbacks g_callbacks;

static int steam_try_quick_join(const char *quick_mode, unsigned long long *joined_lobby_id)
{
    SteamAPICall_t call;
    int i;
    int fallback_index = -1;

    if (!quick_mode)
        return 0;

    steam_reset_browser_results();
    g_lobby_list_pending = 1;
    SteamMatchmaking()->AddRequestLobbyListStringFilter("game", "doom-steam", k_ELobbyComparisonEqual);
    SteamMatchmaking()->AddRequestLobbyListStringFilter("mode", quick_mode, k_ELobbyComparisonEqual);
    SteamMatchmaking()->AddRequestLobbyListFilterSlotsAvailable(1);
    SteamMatchmaking()->AddRequestLobbyListResultCountFilter(STEAM_BROWSER_MAX_LOBBIES);
    call = SteamMatchmaking()->RequestLobbyList();
    g_callbacks.lobby_list_call.Set(call, &g_callbacks, &DoomSteamCallbacks::OnLobbyMatchList);

    if (!steam_wait_for_lobby_list(5000) || g_lobby_count <= 0)
        return 0;

    for (i = 0; i < g_lobby_count; ++i)
    {
        if (!g_browser_lobbies[i].lobby_id)
            continue;
        if (g_browser_lobbies[i].member_limit > 0
            && g_browser_lobbies[i].members >= g_browser_lobbies[i].member_limit)
        {
            continue;
        }

        if (g_browser_lobbies[i].quickplay)
        {
            fallback_index = i;
            break;
        }

        if (fallback_index < 0)
            fallback_index = i;
    }

    if (fallback_index < 0)
        return 0;

    call = SteamMatchmaking()->JoinLobby(CSteamID(g_browser_lobbies[fallback_index].lobby_id));
    g_callbacks.lobby_enter_call.Set(call, &g_callbacks, &DoomSteamCallbacks::OnLobbyEnter);
    steam_logf("quick join requested for %s lobby %llu",
               quick_mode,
               g_browser_lobbies[fallback_index].lobby_id);
    if (!steam_wait_for_lobby(8000) || !g_current_lobby_id)
        return 0;

    if (joined_lobby_id)
        *joined_lobby_id = g_current_lobby_id;
    return 1;
}

#endif

static void steam_log(const char *msg)
{
    printf("[steam] %s\n", msg);
}

static void steam_logf(const char *fmt, ...)
{
    char buffer[1024];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    steam_log(buffer);
}

static int file_exists(const char *path)
{
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static int read_steam_appid_txt(const char *path)
{
    FILE *f;
    int appid = 0;

    f = fopen(path, "rb");
    if (!f)
        return 0;

    if (fscanf(f, "%d", &appid) != 1)
        appid = 0;

    fclose(f);
    return appid;
}

static int has_switch(int argc, char **argv, const char *name)
{
    int i;

    for (i = 1; i < argc; ++i)
    {
        if (!argv[i])
            continue;
        if (_stricmp(argv[i], name) == 0)
            return 1;
    }

    return 0;
}

static const char *switch_value(int argc, char **argv, const char *name)
{
    int i;

    for (i = 1; i + 1 < argc; ++i)
    {
        if (!argv[i])
            continue;
        if (_stricmp(argv[i], name) == 0)
            return argv[i + 1];
    }

    return NULL;
}

static int resolve_appid(int argc, char **argv)
{
    const char *arg_appid;
    const char *env_appid;
    int parsed;

    arg_appid = switch_value(argc, argv, "-steamappid");
    if (arg_appid && arg_appid[0])
    {
        parsed = atoi(arg_appid);
        if (parsed > 0)
            return parsed;
    }

    env_appid = getenv("DOOM_STEAM_APPID");
    if (env_appid && env_appid[0])
    {
        parsed = atoi(env_appid);
        if (parsed > 0)
            return parsed;
    }

    // Default to test app 480 - this limits lobby visibility!
    // To make lobbies visible to other Steam users, set the correct app ID via:
    //   1. Command-line: -steamappid <APPID>
    //   2. Environment: DOOM_STEAM_APPID=<APPID>
    //   3. steam_appid.txt file in game directory
    return 480;
}

static void log_runtime_context(void)
{
    char exe_path[MAX_PATH];
    char exe_dir[MAX_PATH];
    char cwd[MAX_PATH];
    char dll_path[MAX_PATH];
    char appid_in_exe_dir[MAX_PATH];
    char appid_in_cwd[MAX_PATH];
    DWORD exe_len;
    DWORD cwd_len;
    int appid_exe;
    int appid_cwd;

    exe_path[0] = '\0';
    exe_dir[0] = '\0';
    cwd[0] = '\0';

    exe_len = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    if (exe_len > 0 && exe_len < sizeof(exe_path))
    {
        char *slash = strrchr(exe_path, '\\');
        if (slash)
        {
            size_t n = (size_t)(slash - exe_path);
            if (n < sizeof(exe_dir))
            {
                memcpy(exe_dir, exe_path, n);
                exe_dir[n] = '\0';
            }
        }
    }

    cwd_len = GetCurrentDirectoryA(sizeof(cwd), cwd);
    if (cwd_len == 0 || cwd_len >= sizeof(cwd))
        cwd[0] = '\0';

    steam_logf("requested app id: %d", g_requested_appid);
    steam_logf("restart check: %s", g_enable_restart_check ? "enabled" : "disabled");

    if (exe_path[0])
        steam_logf("exe path: %s", exe_path);
    if (cwd[0])
        steam_logf("working directory: %s", cwd);

    if (exe_dir[0])
    {
        snprintf(dll_path, sizeof(dll_path), "%s\\steam_api64.dll", exe_dir);
        steam_logf("steam_api64.dll near exe: %s", file_exists(dll_path) ? "present" : "missing");

        snprintf(appid_in_exe_dir, sizeof(appid_in_exe_dir), "%s\\steam_appid.txt", exe_dir);
        appid_exe = read_steam_appid_txt(appid_in_exe_dir);
        if (appid_exe > 0)
            steam_logf("steam_appid.txt near exe: %d", appid_exe);
        else
            steam_log("steam_appid.txt near exe: missing or invalid");
    }

    if (cwd[0])
    {
        snprintf(appid_in_cwd, sizeof(appid_in_cwd), "%s\\steam_appid.txt", cwd);
        appid_cwd = read_steam_appid_txt(appid_in_cwd);
        if (appid_cwd > 0)
            steam_logf("steam_appid.txt in cwd: %d", appid_cwd);
        else
            steam_log("steam_appid.txt in cwd: missing or invalid");
    }
}

static int steam_init_common(int argc, char **argv, int allow_restart)
{
#ifdef DOOM_ENABLE_STEAMWORKS
    if (g_active)
        return 1;

    g_requested_appid = resolve_appid(argc, argv);
    g_enable_restart_check = allow_restart && has_switch(argc, argv, "-steamrestart");

    steam_log("initializing steam transport");
    log_runtime_context();

    if (g_enable_restart_check)
    {
        if (SteamAPI_RestartAppIfNecessary((uint32)g_requested_appid))
        {
            snprintf(g_last_error,
                     sizeof(g_last_error),
                     "Steam requested restart through client for app id %d",
                     g_requested_appid);
            steam_log(g_last_error);
            return 0;
        }
        steam_log("SteamAPI_RestartAppIfNecessary: no restart needed");
    }
    else
    {
        steam_log("restart check disabled (use -steamrestart to enable)");
    }

    if (!SteamAPI_Init())
    {
        snprintf(g_last_error,
                 sizeof(g_last_error),
                 "SteamAPI_Init failed (is Steam running and appid configured?)");
        steam_log(g_last_error);
        return 0;
    }

    if (!SteamUser() || !SteamUtils() || !SteamFriends() || !SteamMatchmaking() || !SteamNetworking())
    {
        snprintf(g_last_error,
                 sizeof(g_last_error),
                 "SteamAPI initialized but one or more required interfaces are unavailable");
        steam_log(g_last_error);
        SteamAPI_Shutdown();
        return 0;
    }

    if (!SteamUser()->BLoggedOn())
    {
        snprintf(g_last_error,
                 sizeof(g_last_error),
                 "Steam user is not logged on");
        steam_log(g_last_error);
        SteamAPI_Shutdown();
        return 0;
    }

    steam_logf("SteamAPI_Init succeeded for app id %u", (unsigned)SteamUtils()->GetAppID());
    
    // shut that shit up. that's not even remotely true. ~vee<3
    
    g_active = 1;
    return 1;
#else
    (void)argc;
    (void)argv;
    (void)allow_restart;
    snprintf(g_last_error,
             sizeof(g_last_error),
             "Steamworks SDK not enabled. Configure with -DDOOM_ENABLE_STEAMWORKS=ON and provide steam_api64.lib");
    return 0;
#endif
}

int STEAM_InitTransport(doomcom_t *doomcom, int argc, char **argv)
{
    int wants_steam;
    int host_mode;
    const char *quick_mode;
    const char *join_lobby;
    unsigned long long lobby_id_from_secret;
    char join_lobby_buf[64];

    if (!doomcom)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Steam init failed: invalid doomcom");
        return 0;
    }

#ifdef DOOM_ENABLE_STEAMWORKS
    quick_mode = steam_quick_mode_from_args(argc, argv);
#else
    quick_mode = NULL;
#endif

    wants_steam = has_switch(argc, argv, "-steam")
        || has_switch(argc, argv, "-steamhost")
        || switch_value(argc, argv, "-steamjoin") != NULL
        || quick_mode != NULL;

    join_lobby = switch_value(argc, argv, "-steamjoin");
    if (!join_lobby)
    {
#ifdef DOOM_ENABLE_STEAMWORKS
        lobby_id_from_secret = steam_lobby_id_from_secret(switch_value(argc, argv, "-discordjoinsecret"));
#else
        lobby_id_from_secret = 0;
#endif
        if (lobby_id_from_secret)
        {
            snprintf(join_lobby_buf, sizeof(join_lobby_buf), "%llu", lobby_id_from_secret);
            join_lobby = join_lobby_buf;
            wants_steam = 1;
            steam_logf("using lobby id %s from discord join secret", join_lobby_buf);
        }
    }

    if (!wants_steam)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Steam mode not requested (use -steamhost or -steamjoin <lobby_id>)");
        return 0;
    }

    host_mode = has_switch(argc, argv, "-steamhost");

    if (host_mode && join_lobby)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Steam init failed: choose one of -steamhost or -steamjoin");
        return 0;
    }

    if ((host_mode || join_lobby) && quick_mode)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Steam init failed: -steamquick cannot be combined with -steamhost/-steamjoin");
        return 0;
    }

#ifdef DOOM_ENABLE_STEAMWORKS
    if (!steam_init_common(argc, argv, 1))
        return 0;

    g_doomcom = doomcom;
    g_host_mode = host_mode;
    g_current_lobby_id = 0;
    g_node_count = 0;
    g_console_node = 0;

    if (quick_mode)
    {
        unsigned long long quick_join_lobby_id = 0;

        if (steam_try_quick_join(quick_mode, &quick_join_lobby_id))
        {
            g_host_mode = 0;
            steam_logf("quick joined %s lobby %llu", quick_mode, quick_join_lobby_id);
        }
        else
        {
            SteamAPICall_t call = SteamMatchmaking()->CreateLobby(k_ELobbyTypePublic, STEAM_MAX_PLAYERS);
            g_host_mode = 1;
            g_callbacks.lobby_created_call.Set(call, &g_callbacks, &DoomSteamCallbacks::OnLobbyCreated);
            steam_logf("no quick %s lobby found, creating one", quick_mode);
            steam_wait_for_lobby(8000);
            host_mode = 1;
        }
    }
    else if (host_mode)
    {
        SteamAPICall_t call = SteamMatchmaking()->CreateLobby(k_ELobbyTypePublic, STEAM_MAX_PLAYERS);
        g_callbacks.lobby_created_call.Set(call, &g_callbacks, &DoomSteamCallbacks::OnLobbyCreated);
        steam_log("requested lobby creation");
        steam_wait_for_lobby(8000);
    }
    else if (join_lobby)
    {
        unsigned long long lobby_id = _strtoui64(join_lobby, NULL, 10);
        SteamAPICall_t call;

        if (!lobby_id)
        {
            snprintf(g_last_error, sizeof(g_last_error), "Steam join failed: invalid lobby id");
            steam_log(g_last_error);
            return 0;
        }

        call = SteamMatchmaking()->JoinLobby(CSteamID(lobby_id));
        g_callbacks.lobby_enter_call.Set(call, &g_callbacks, &DoomSteamCallbacks::OnLobbyEnter);
        steam_logf("requested lobby join for %llu", lobby_id);
        steam_wait_for_lobby(8000);

        // Startup netcode is decided early; wait briefly for host presence
        // so we don't lock into a 1-node single-player session due to timing.
        steam_wait_for_min_members(2, 5000);
    }

    if (g_current_lobby_id)
        steam_refresh_node_map();

    if (g_node_count < 1)
    {
        g_node_count = 1;
        g_console_node = 0;
    }

    doomcom->numnodes = g_node_count;
    doomcom->numplayers = g_node_count;
    doomcom->consoleplayer = g_console_node;

    if (host_mode && doomcom->numnodes < 2 && !has_switch(argc, argv, "-steamallowsolo"))
    {
        doomcom->numnodes = 2;
        doomcom->numplayers = 2;
        doomcom->consoleplayer = 0;
        steam_log("host waiting for first peer (use -steamallowsolo to bypass)");
    }

    netgame = true;

    g_next_member_refresh_ms = GetTickCount() + 250;
    snprintf(g_last_error, sizeof(g_last_error), "Steam transport active");
    return 1;
#else
    snprintf(g_last_error,
             sizeof(g_last_error),
             "Steamworks SDK not enabled. Configure with -DDOOM_ENABLE_STEAMWORKS=ON and provide steam_api64.lib");
    return 0;
#endif
}

void STEAM_ShutdownTransport(void)
{
#ifdef DOOM_ENABLE_STEAMWORKS
    if (g_active)
    {
        int i;
        CSteamID self = SteamUser() ? SteamUser()->GetSteamID() : CSteamID();

        for (i = 0; i < g_node_count; ++i)
        {
            if (!g_node_ids[i].IsValid())
                continue;
            if (self.IsValid() && g_node_ids[i].ConvertToUint64() == self.ConvertToUint64())
                continue;
            SteamNetworking()->CloseP2PSessionWithUser(g_node_ids[i]);
        }

        g_current_lobby_id = 0;
        g_lobby_list_pending = 0;
        g_host_mode = 0;
        g_node_count = 0;
        g_console_node = 0;
        steam_reset_browser_results();
        I_DiscordRPC_SetSteamJoin(0, 0, 0, NULL, NULL);
        steam_log("SteamAPI_Shutdown");
        SteamAPI_Shutdown();
    }
#endif
    g_doomcom = NULL;
    g_active = 0;
}

int STEAM_IsActive(void)
{
    return g_active;
}

int STEAM_EnsureClient(void)
{
    return steam_init_common(myargc, myargv, 0);
}

int STEAM_CreatePublicLobby(void)
{
#ifdef DOOM_ENABLE_STEAMWORKS
    SteamAPICall_t call;

    if (!STEAM_EnsureClient())
        return 0;

    g_host_mode = 1;
    g_current_lobby_id = 0;
    call = SteamMatchmaking()->CreateLobby(k_ELobbyTypePublic, STEAM_MAX_PLAYERS);
    g_callbacks.lobby_created_call.Set(call, &g_callbacks, &DoomSteamCallbacks::OnLobbyCreated);
    steam_log("requested lobby creation");
    return 1;
#else
    snprintf(g_last_error,
             sizeof(g_last_error),
             "Steamworks SDK not enabled. Configure with -DDOOM_ENABLE_STEAMWORKS=ON and provide steam_api64.lib");
    return 0;
#endif
}

int STEAM_JoinLobbyById(unsigned long long lobby_id)
{
#ifdef DOOM_ENABLE_STEAMWORKS
    SteamAPICall_t call;

    if (!lobby_id)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Steam join failed: invalid lobby id");
        return 0;
    }

    if (!STEAM_EnsureClient())
        return 0;

    g_host_mode = 0;
    g_current_lobby_id = 0;
    call = SteamMatchmaking()->JoinLobby(CSteamID(lobby_id));
    g_callbacks.lobby_enter_call.Set(call, &g_callbacks, &DoomSteamCallbacks::OnLobbyEnter);
    steam_logf("requested lobby join for %llu", lobby_id);
    return 1;
#else
    (void)lobby_id;
    snprintf(g_last_error,
             sizeof(g_last_error),
             "Steamworks SDK not enabled. Configure with -DDOOM_ENABLE_STEAMWORKS=ON and provide steam_api64.lib");
    return 0;
#endif
}

void STEAM_Update(void)
{
#ifdef DOOM_ENABLE_STEAMWORKS
    if (g_active)
    {
        SteamAPI_RunCallbacks();

        if (g_current_lobby_id && GetTickCount() >= g_next_member_refresh_ms)
        {
            steam_refresh_node_map();
            g_next_member_refresh_ms = GetTickCount() + 250;
        }
    }
#endif
}

int STEAM_RequestLobbyList(void)
{
#ifdef DOOM_ENABLE_STEAMWORKS
    SteamAPICall_t call;

    if (!STEAM_EnsureClient())
        return 0;

    steam_reset_browser_results();
    g_lobby_list_pending = 1;
    SteamMatchmaking()->AddRequestLobbyListStringFilter("game", "doom-steam", k_ELobbyComparisonEqual);
    SteamMatchmaking()->AddRequestLobbyListResultCountFilter(STEAM_BROWSER_MAX_LOBBIES);
    call = SteamMatchmaking()->RequestLobbyList();
    g_callbacks.lobby_list_call.Set(call, &g_callbacks, &DoomSteamCallbacks::OnLobbyMatchList);
    steam_log("requested public lobby list");
    return 1;
#else
    snprintf(g_last_error,
             sizeof(g_last_error),
             "Steamworks SDK not enabled. Configure with -DDOOM_ENABLE_STEAMWORKS=ON and provide steam_api64.lib");
    return 0;
#endif
}

int STEAM_IsLobbyListPending(void)
{
#ifdef DOOM_ENABLE_STEAMWORKS
    return g_lobby_list_pending;
#else
    return 0;
#endif
}

int STEAM_GetLobbyCount(void)
{
#ifdef DOOM_ENABLE_STEAMWORKS
    return g_lobby_count;
#else
    return 0;
#endif
}

int STEAM_GetLobbyInfo(int index,
                       char *title,
                       int title_size,
                       char *details,
                       int details_size,
                       unsigned long long *lobby_id)
{
#ifdef DOOM_ENABLE_STEAMWORKS
    if (index < 0 || index >= g_lobby_count)
        return 0;

    if (title && title_size > 0)
        snprintf(title, title_size, "%s", g_browser_lobbies[index].title);
    if (details && details_size > 0)
        snprintf(details, details_size, "%s", g_browser_lobbies[index].details);
    if (lobby_id)
        *lobby_id = g_browser_lobbies[index].lobby_id;
    return 1;
#else
    (void)index;
    (void)title;
    (void)title_size;
    (void)details;
    (void)details_size;
    (void)lobby_id;
    return 0;
#endif
}

void STEAM_NetCmd(doomcom_t *doomcom)
{
    if (!doomcom)
        return;

    STEAM_Update();

#ifdef DOOM_ENABLE_STEAMWORKS
    if (doomcom->command == CMD_SEND)
    {
        int node = doomcom->remotenode;

        if (node < 0 || node >= g_node_count || node >= MAXNETNODES)
            return;
        if (!g_node_ids[node].IsValid())
            return;
        if (node == g_console_node)
            return;

        if (!SteamNetworking()->SendP2PPacket(g_node_ids[node],
                                              &doomcom->data,
                                              (uint32)doomcom->datalength,
                                              k_EP2PSendUnreliableNoDelay,
                                              0))
        {
            snprintf(g_last_error, sizeof(g_last_error), "SendP2PPacket failed for node %d", node);
        }
        return;
    }

    if (doomcom->command == CMD_GET)
    {
        uint32 packet_size = 0;
        CSteamID sender;
        uint32 bytes_read = 0;
        int node;

        if (!SteamNetworking()->IsP2PPacketAvailable(&packet_size, 0))
        {
            doomcom->remotenode = -1;
            doomcom->datalength = 0;
            return;
        }

        if (packet_size > sizeof(doomcom->data))
        {
            std::vector<unsigned char> junk(packet_size);
            SteamNetworking()->ReadP2PPacket(junk.data(), packet_size, &bytes_read, &sender, 0);
            doomcom->remotenode = -1;
            doomcom->datalength = 0;
            return;
        }

        if (!SteamNetworking()->ReadP2PPacket(&doomcom->data,
                                              (uint32)sizeof(doomcom->data),
                                              &bytes_read,
                                              &sender,
                                              0))
        {
            doomcom->remotenode = -1;
            doomcom->datalength = 0;
            return;
        }

        node = steam_find_node_for_user(sender);
        if (node < 0)
        {
            steam_refresh_node_map();
            node = steam_find_node_for_user(sender);
        }

        if (node < 0)
        {
            doomcom->remotenode = -1;
            doomcom->datalength = 0;
            return;
        }

        doomcom->remotenode = (short)node;
        doomcom->datalength = (short)bytes_read;
        return;
    }
#endif

    if (doomcom->command == CMD_GET)
    {
        doomcom->remotenode = -1;
        doomcom->datalength = 0;
    }
}

const char *STEAM_LastError(void)
{
    return g_last_error;
}
