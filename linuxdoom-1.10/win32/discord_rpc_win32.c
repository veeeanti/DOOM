//----------------------------------------------------------
//            DOOM'93 Discord Rich Presence
//
//  Cross-platform Discord integration with game status
//
//  veeλnti is responsible for this
//----------------------------------------------------------

#ifdef _WIN32
#define boolean windows_boolean_workaround
#include <windows.h>
#include <stdint.h>
#undef boolean
#else
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

typedef int HANDLE;
typedef uint32_t DWORD;

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE (-1)
#endif

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "doomstat.h"
#include "doomdef.h"
#include "w_wad.h"
#include "win32/discord_rpc_win32.h"

// Discord RPC implementation
#define DISCORD_OP_HANDSHAKE 0
#define DISCORD_OP_FRAME 1
#define DISCORD_OP_CLOSE 3

static int s_discord_enabled = 0;
static int s_discord_connected = 0;
static int s_presence_serial = 0;
static char s_app_id[64] = {0};
static char s_join_secret[128] = {0};
static char s_party_id[128] = {0};
static char s_presence_mode[64] = {0};
static char s_presence_content[128] = {0};
static int s_party_size = 0;
static int s_party_max = 0;
static gamestate_t s_last_gamestate = (gamestate_t)-1;
static int s_last_netgame = -1;
static int s_last_deathmatch = -1;
static int s_last_update_tic = 0;

// WAD/mod name storage (set by I_SetWadName)
static char s_wad_name[64] = {0};

void I_SetWadName(const char *wad_name)
{
    if (wad_name)
    {
        strncpy(s_wad_name, wad_name, sizeof(s_wad_name) - 1);
        s_wad_name[sizeof(s_wad_name) - 1] = '\0';
    }
    else
    {
        s_wad_name[0] = '\0';
    }
}

#ifdef _WIN32
static HANDLE s_pipe = INVALID_HANDLE_VALUE;
#else
static int s_pipe = -1;
#endif

static void close_pipe(void)
{
#ifdef _WIN32
    if (s_pipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(s_pipe);
        s_pipe = INVALID_HANDLE_VALUE;
    }
#else
    if (s_pipe >= 0)
    {
        close(s_pipe);
        s_pipe = -1;
    }
#endif
}

static int connect_pipe(void)
{
    close_pipe();

#ifdef _WIN32
    char pipe_name[256];
    snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\discord-ipc-0");
    s_pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (s_pipe == INVALID_HANDLE_VALUE)
        return 0;
#else
    char pipe_name[256];
    const char *temp_path = getenv("XDG_RUNTIME_DIR");
    if (!temp_path)
        temp_path = "/tmp";
    snprintf(pipe_name, sizeof(pipe_name), "%s/discord-ipc-0", temp_path);
    s_pipe = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s_pipe < 0)
        return 0;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, pipe_name, sizeof(addr.sun_path) - 1);
    if (connect(s_pipe, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(s_pipe);
        s_pipe = -1;
        return 0;
    }
#endif

    s_discord_connected = 1;
    return 1;
}

static int send_frame(int opcode, const char *data)
{
    if (!s_discord_connected)
        return 0;

    size_t data_len = strlen(data);
    uint32_t header[2] = {opcode, (uint32_t)data_len};

#ifdef _WIN32
    DWORD written;
    if (!WriteFile(s_pipe, header, sizeof(header), &written, NULL))
        return 0;
    if (!WriteFile(s_pipe, data, data_len, &written, NULL))
        return 0;
#else
    if (write(s_pipe, header, sizeof(header)) != sizeof(header))
        return 0;
    if (write(s_pipe, data, data_len) != (ssize_t)data_len)
        return 0;
#endif

    return 1;
}

static void send_handshake(void)
{
    char handshake[256];
    snprintf(handshake, sizeof(handshake),
             "{\"v\": 1, \"client_id\": \"%s\"}", s_app_id);
    send_frame(DISCORD_OP_HANDSHAKE, handshake);
}

static void send_clear_presence(void)
{
    send_frame(DISCORD_OP_FRAME,
               "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":0},\"nonce\":\"0\"}");
}

void I_DiscordRPC_Init(void)
{
    const char *app_id = getenv("DOOM_DISCORD_APP_ID");
    if (!app_id)
        app_id = "1485899956291112990"; // Default app ID

    strncpy(s_app_id, app_id, sizeof(s_app_id) - 1);
    s_app_id[sizeof(s_app_id) - 1] = '\0';

    if (connect_pipe())
    {
        send_handshake();
        s_discord_enabled = 1;
    }
}

void I_DiscordRPC_Update(void)
{
    if (!s_discord_enabled || !s_discord_connected)
        return;

    // Update every 5 seconds to avoid rate limiting
    static int last_update = 0;
    int current_tic = gametic;
    if (current_tic - last_update < TICRATE * 5)
        return;
    last_update = current_tic;

    // Only update if something changed
    if (gamestate == s_last_gamestate &&
        netgame == s_last_netgame &&
        deathmatch == s_last_deathmatch)
    {
        return;
    }

    s_last_gamestate = gamestate;
    s_last_netgame = netgame;
    s_last_deathmatch = deathmatch;

    char presence[1024];
    const char *state = "In Menu";
    const char *details = "";
    char game_title[64] = {0};
    char episode_name[64] = {0};

    // Use stored WAD name if available, otherwise detect from game state
    if (s_wad_name[0])
    {
        strncpy(game_title, s_wad_name, sizeof(game_title) - 1);
    }
    else if (gamemode == commercial)
    {
        // Check for specific commercial WADs based on game behavior
        if (gameepisode == 0)
        {
            // DOOM II / Final DOOM / TNT / Plutonia
            strncpy(game_title, "DOOM II", sizeof(game_title) - 1);
        }
    }
    else
    {
        // DOOM I / Ultimate DOOM
        if (gameepisode == 4)
        {
            strncpy(game_title, "Ultimate DOOM", sizeof(game_title) - 1);
        }
        else
        {
            strncpy(game_title, "DOOM", sizeof(game_title) - 1);
        }
    }

    // Get episode and map name
    if (gamemode == commercial)
    {
        // DOOM II / Final DOOM / TNT / Plutonia
        snprintf(episode_name, sizeof(episode_name), "MAP%02d", gamemap);
    }
    else
    {
        // DOOM I episodes
        const char *map_names[] = {
            "E1M1: The Hangar", "E1M2: Nuclear Plant", "E1M3: Toxin Refinery", 
            "E1M4: Command Control", "E1M5: Phobos Lab", "E1M6: Central Processing", 
            "E1M7: Computer Station", "E1M8: Phobos Anomaly", "E1M9: Military Base",
            "E2M1: Refueling Bay", "E2M2: Deimos Lab", "E2M3: The Factory", 
            "E2M4: Deimos Anomaly", "E2M5: Containment Area", "E2M6: Halls of the Damned", 
            "E2M7: Spawning Vats", "E2M8: The Chasm", "E2M9: Fortress of Mystery",
            "E3M1: Hell Keep", "E3M2: Slough of Despair", "E3M3: Pandemonium", 
            "E3M4: House of Pain", "E3M5: Unholy Cathedral", "E3M6: Mt. Erebus", 
            "E3M7: Limbo", "E3M8: Dis", "E3M9: Warrens",
            "E4M1: Hell Beneath", "E4M2: Perfect Hatred", "E4M3: Sever the Wicked", 
            "E4M4: Unruly Evil", "E4M5: They Will Repent", "E4M6: Against Thee Wickedly", 
            "E4M7: And Hell Followed", "E4M8: Unto the Cruel", "E4M9: Fear"
        };
        
        if (gameepisode >= 1 && gameepisode <= 4 && gamemap >= 1 && gamemap <= 9)
        {
            int map_idx = (gameepisode - 1) * 9 + (gamemap - 1);
            if (map_idx < 36)
            {
                strncpy(episode_name, map_names[map_idx], sizeof(episode_name) - 1);
            }
            else
            {
                snprintf(episode_name, sizeof(episode_name), "E%dM%d", gameepisode, gamemap);
            }
        }
        else
        {
            snprintf(episode_name, sizeof(episode_name), "E%dM%d", gameepisode, gamemap);
        }
    }

    switch (gamestate)
    {
    case GS_LEVEL:
        // Since it's only single player, use WAD/mod name as state
        if (s_wad_name[0])
        {
            state = s_wad_name;
        }
        else
        {
            state = "Single Player";
        }
        details = episode_name;
        break;
    case GS_INTERMISSION:
        state = "Intermission";
        details = "Between Levels";
        break;
    case GS_FINALE:
        state = "Finale";
        details = "Game Completed";
        break;
    case GS_DEMOSCREEN:
        state = "Demo";
        details = "Watching Demo";
        break;
    }

    // Build JSON payload with game title
    if (game_title[0])
    {
        if (s_party_id[0] && s_party_size > 0 && s_party_max > 0)
        {
            snprintf(presence, sizeof(presence),
                     "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,"
                     "\"activity\":{"
                     "\"state\":\"%s\","
                     "\"details\":\"%s\","
                     "\"timestamps\":{\"start\":%d},"
                     "\"assets\":{"
                     "\"large_image\":\"doom-icon\","
                     "\"large_text\":\"%s\""
                     "},"
                     "\"party\":{"
                     "\"id\":\"%s\","
                     "\"size\":[%d,%d]"
                     "},"
                     "\"secrets\":{"
                     "\"join\":\"%s\""
                     "}"
                     "}},\"nonce\":\"%d\"}",
                     getpid(), state, details, (int)time(NULL), game_title,
                     s_party_id, s_party_size, s_party_max,
                     s_join_secret, s_presence_serial);
        }
        else
        {
            snprintf(presence, sizeof(presence),
                     "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,"
                     "\"activity\":{"
                     "\"state\":\"%s\","
                     "\"details\":\"%s\","
                     "\"timestamps\":{\"start\":%d},"
                     "\"assets\":{"
                     "\"large_image\":\"doom-icon\","
                     "\"large_text\":\"%s\""
                     "}"
                     "}},\"nonce\":\"%d\"}",
                     getpid(), state, details, (int)time(NULL), game_title,
                     s_presence_serial);
        }
    }
    else
    {
        // Fallback
        if (s_party_id[0] && s_party_size > 0 && s_party_max > 0)
        {
            snprintf(presence, sizeof(presence),
                     "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,"
                     "\"activity\":{"
                     "\"state\":\"%s\","
                     "\"details\":\"%s\","
                     "\"timestamps\":{\"start\":%d},"
                     "\"assets\":{"
                     "\"large_image\":\"doom-icon\","
                     "\"large_text\":\"DOOM\""
                     "},"
                     "\"party\":{"
                     "\"id\":\"%s\","
                     "\"size\":[%d,%d]"
                     "},"
                     "\"secrets\":{"
                     "\"join\":\"%s\""
                     "}"
                     "}},\"nonce\":\"%d\"}",
                     getpid(), state, details, (int)time(NULL),
                     s_party_id, s_party_size, s_party_max,
                     s_join_secret, s_presence_serial);
        }
        else
        {
            snprintf(presence, sizeof(presence),
                     "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,"
                     "\"activity\":{"
                     "\"state\":\"%s\","
                     "\"details\":\"%s\","
                     "\"timestamps\":{\"start\":%d},"
                     "\"assets\":{"
                     "\"large_image\":\"doom-icon\","
                     "\"large_text\":\"DOOM\""
                     "}"
                     "}},\"nonce\":\"%d\"}",
                     getpid(), state, details, (int)time(NULL),
                     s_presence_serial);
        }
    }

    send_frame(DISCORD_OP_FRAME, presence);
}

void I_DiscordRPC_Shutdown(void)
{
    if (!s_discord_enabled)
        return;

    if (s_discord_connected)
    {
        send_clear_presence();
        // Don't send close frame as it causes issues
    }

    close_pipe();
    s_discord_enabled = 0;
}

void I_DiscordRPC_SetSteamJoin(unsigned long long lobby_id,
                               int party_size,
                               int party_max,
                               const char *mode,
                               const char *content)
{
    // Disabled Steam integration
    s_join_secret[0] = '\0';
    s_party_id[0] = '\0';
    s_presence_mode[0] = '\0';
    s_presence_content[0] = '\0';
    ++s_presence_serial;
}