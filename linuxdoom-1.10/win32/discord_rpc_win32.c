#define boolean windows_boolean_workaround
#include <windows.h>
#undef boolean

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_argv.h"

#include "win32/discord_rpc_win32.h"

enum
{
    DISCORD_OP_HANDSHAKE = 0,
    DISCORD_OP_FRAME = 1,
    DISCORD_OP_CLOSE = 2
};

static HANDLE s_pipe = INVALID_HANDLE_VALUE;
static int s_discord_enabled;
static int s_discord_connected;
static int s_last_connect_attempt_tic;
static int s_last_update_tic;
static unsigned int s_nonce;
static char s_app_id[64];
static gamestate_t s_last_gamestate = (gamestate_t)-1;
static int s_last_gameepisode = -1;
static int s_last_gamemap = -1;
static int s_last_netgame = -1;
static int s_last_deathmatch = -1;

// Default Discord application id used when no override is provided.
static const char *s_default_app_id = "1485899956291112990";

static const char *resolve_discord_app_id(void)
{
    int p = M_CheckParm("-discordappid");
    if (p && p + 1 < myargc)
        return myargv[p + 1];

    {
        const char *env_id = getenv("DISCORD_APP_ID");
        if (env_id && env_id[0])
            return env_id;
    }

    return s_default_app_id;
}

static void close_pipe(void)
{
    if (s_pipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(s_pipe);
        s_pipe = INVALID_HANDLE_VALUE;
    }

    s_discord_connected = 0;
}

static int send_frame(int opcode, const char *json)
{
    DWORD header[2];
    DWORD written;
    DWORD json_len = (DWORD)strlen(json);

    if (s_pipe == INVALID_HANDLE_VALUE)
        return 0;

    header[0] = (DWORD)opcode;
    header[1] = json_len;

    if (!WriteFile(s_pipe, header, sizeof(header), &written, NULL) || written != sizeof(header))
        return 0;

    if (json_len > 0)
    {
        if (!WriteFile(s_pipe, json, json_len, &written, NULL) || written != json_len)
            return 0;
    }

    return 1;
}

static void drain_incoming_frames(void)
{
    DWORD available;

    if (s_pipe == INVALID_HANDLE_VALUE)
        return;

    while (PeekNamedPipe(s_pipe, NULL, 0, NULL, &available, NULL) && available >= 8)
    {
        DWORD header[2];
        DWORD read;
        DWORD payload_len;
        char *payload;

        if (!ReadFile(s_pipe, header, sizeof(header), &read, NULL) || read != sizeof(header))
        {
            close_pipe();
            return;
        }

        payload_len = header[1];
        if (payload_len == 0)
            continue;

        payload = (char *)malloc(payload_len + 1);
        if (!payload)
        {
            close_pipe();
            return;
        }

        if (!ReadFile(s_pipe, payload, payload_len, &read, NULL) || read != payload_len)
        {
            free(payload);
            close_pipe();
            return;
        }

        payload[payload_len] = '\0';
        free(payload);
    }
}

static int connect_and_handshake(void)
{
    int i;
    char pipe_name[64];
    char handshake[160];

    for (i = 0; i < 10; ++i)
    {
        snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\discord-ipc-%d", i);

        s_pipe = CreateFileA(pipe_name,
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
        if (s_pipe != INVALID_HANDLE_VALUE)
            break;
    }

    if (s_pipe == INVALID_HANDLE_VALUE)
        return 0;

    snprintf(handshake, sizeof(handshake), "{\"v\":1,\"client_id\":\"%s\"}", s_app_id);
    if (!send_frame(DISCORD_OP_HANDSHAKE, handshake))
    {
        close_pipe();
        return 0;
    }

    s_discord_connected = 1;
    drain_incoming_frames();
    return 1;
}

static void json_escape(char *dst, size_t dst_size, const char *src)
{
    size_t di = 0;

    if (!dst_size)
        return;

    while (*src && di + 1 < dst_size)
    {
        unsigned char c = (unsigned char)*src++;

        if (c == '"' || c == '\\')
        {
            if (di + 2 >= dst_size)
                break;
            dst[di++] = '\\';
            dst[di++] = (char)c;
            continue;
        }

        if (c == '\n')
        {
            if (di + 2 >= dst_size)
                break;
            dst[di++] = '\\';
            dst[di++] = 'n';
            continue;
        }

        if (c == '\r')
        {
            if (di + 2 >= dst_size)
                break;
            dst[di++] = '\\';
            dst[di++] = 'r';
            continue;
        }

        if (c == '\t')
        {
            if (di + 2 >= dst_size)
                break;
            dst[di++] = '\\';
            dst[di++] = 't';
            continue;
        }

        if (c < 0x20)
            continue;

        dst[di++] = (char)c;
    }

    dst[di] = '\0';
}

static void get_presence_strings(char *details, size_t details_size,
                                 char *state, size_t state_size)
{
    if (gamestate == GS_LEVEL)
    {
        if (gamemode == commercial)
            snprintf(details, details_size, "MAP%02d", gamemap);
        else
            snprintf(details, details_size, "E%dM%d", gameepisode, gamemap);

        if (deathmatch)
            snprintf(state, state_size, "Deathmatch");
        else if (netgame)
            snprintf(state, state_size, "Co-op");
        else
            snprintf(state, state_size, "Single Player");

        return;
    }

    switch (gamestate)
    {
    case GS_INTERMISSION:
        snprintf(details, details_size, "Intermission");
        break;
    case GS_FINALE:
        snprintf(details, details_size, "Finale");
        break;
    case GS_DEMOSCREEN:
        snprintf(details, details_size, "Title / Demo");
        break;
    default:
        snprintf(details, details_size, "In Menus");
        break;
    }

    snprintf(state, state_size, "Idle");
}

static int send_activity_update(void)
{
    char details[64];
    char state[64];
    char details_json[128];
    char state_json[128];
    char payload[768];
    char nonce[32];
    int wrote;

    get_presence_strings(details, sizeof(details), state, sizeof(state));
    json_escape(details_json, sizeof(details_json), details);
    json_escape(state_json, sizeof(state_json), state);

    snprintf(nonce, sizeof(nonce), "%u", ++s_nonce);

    if (gamestate == GS_LEVEL)
    {
        long long started = (long long)time(NULL) - (long long)(leveltime / TICRATE);
        wrote = snprintf(payload,
                         sizeof(payload),
                         "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":{\"state\":\"%s\",\"details\":\"%s\",\"timestamps\":{\"start\":%lld},\"assets\":{\"large_image\":\"doomguy\",\"large_text\":\"DOOM\"}}},\"nonce\":\"%s\"}",
                         (unsigned long)GetCurrentProcessId(),
                         state_json,
                         details_json,
                         started,
                         nonce);
    }
    else
    {
        wrote = snprintf(payload,
                         sizeof(payload),
                         "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":{\"state\":\"%s\",\"details\":\"%s\",\"assets\":{\"large_image\":\"doomguy\",\"large_text\":\"DOOM\"}}},\"nonce\":\"%s\"}",
                         (unsigned long)GetCurrentProcessId(),
                         state_json,
                         details_json,
                         nonce);
    }

    if (wrote <= 0 || wrote >= (int)sizeof(payload))
        return 0;

    return send_frame(DISCORD_OP_FRAME, payload);
}

static void send_clear_presence(void)
{
    char payload[256];
    char nonce[32];
    int wrote;

    snprintf(nonce, sizeof(nonce), "%u", ++s_nonce);
    wrote = snprintf(payload,
                     sizeof(payload),
                     "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":null},\"nonce\":\"%s\"}",
                     (unsigned long)GetCurrentProcessId(),
                     nonce);

    if (wrote > 0 && wrote < (int)sizeof(payload))
        send_frame(DISCORD_OP_FRAME, payload);
}

void I_DiscordRPC_Init(void)
{
    const char *app_id;

    if (s_discord_enabled)
        return;

    app_id = resolve_discord_app_id();
    if (!app_id || !app_id[0])
    {
        printf("Discord RPC: disabled (set -discordappid <id> or DISCORD_APP_ID).\n");
        return;
    }

    snprintf(s_app_id, sizeof(s_app_id), "%s", app_id);
    s_discord_enabled = 1;
    s_last_update_tic = -TICRATE * 10;
    s_last_connect_attempt_tic = -TICRATE * 10;

    if (connect_and_handshake())
        printf("Discord RPC: connected (IPC) with app id %s\n", s_app_id);
    else
        printf("Discord RPC: waiting for Discord client (IPC).\n");
}

void I_DiscordRPC_Update(void)
{
    int now_tic;
    int should_update;

    if (!s_discord_enabled)
        return;

    now_tic = I_GetTime();

    if (!s_discord_connected)
    {
        if (now_tic - s_last_connect_attempt_tic >= TICRATE * 5)
        {
            s_last_connect_attempt_tic = now_tic;
            connect_and_handshake();
        }

        if (!s_discord_connected)
            return;
    }

    drain_incoming_frames();

    should_update = 0;
    if (gamestate != s_last_gamestate
        || gameepisode != s_last_gameepisode
        || gamemap != s_last_gamemap
        || netgame != s_last_netgame
        || deathmatch != s_last_deathmatch)
    {
        should_update = 1;
    }

    if (now_tic - s_last_update_tic >= TICRATE * 5)
        should_update = 1;

    if (!should_update)
        return;

    if (!send_activity_update())
    {
        close_pipe();
        return;
    }

    s_last_update_tic = now_tic;
    s_last_gamestate = gamestate;
    s_last_gameepisode = gameepisode;
    s_last_gamemap = gamemap;
    s_last_netgame = netgame;
    s_last_deathmatch = deathmatch;
}

void I_DiscordRPC_Shutdown(void)
{
    if (!s_discord_enabled)
        return;

    if (s_discord_connected)
    {
        send_clear_presence();
        send_frame(DISCORD_OP_CLOSE, "");
    }

    close_pipe();
    s_discord_enabled = 0;
}
