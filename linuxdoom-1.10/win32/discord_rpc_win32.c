#ifdef _WIN32
#define boolean windows_boolean_workaround
#include <windows.h>
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

#define GENERIC_READ  0x1
#define GENERIC_WRITE 0x2
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_NORMAL 0

static int open_discord_unix_socket_from_pipe_name(const char *pipe_name)
{
    const char *dash;
    int index;
    const char *dirs[16];
    const char *explicit_ipc_path;
    int uid;
    char run_user_dir[64];
    char flatpak_stable_dir[128];
    char flatpak_ptb_dir[128];
    char flatpak_canary_dir[128];
    char snap_stable_dir[128];
    char snap_ptb_dir[128];
    char snap_canary_dir[128];
    char explicit_socket_path[108];
    char explicit_socket_dir[108];
    int d;

    if (!pipe_name)
        return -1;

    dash = strrchr(pipe_name, '-');
    if (!dash || !dash[1])
        return -1;

    index = atoi(dash + 1);
    if (index < 0)
        return -1;

    uid = (int)geteuid();
    snprintf(run_user_dir, sizeof(run_user_dir), "/run/user/%d", uid);
    snprintf(flatpak_stable_dir, sizeof(flatpak_stable_dir), "%s/app/com.discordapp.Discord", run_user_dir);
    snprintf(flatpak_ptb_dir, sizeof(flatpak_ptb_dir), "%s/app/com.discordapp.DiscordPTB", run_user_dir);
    snprintf(flatpak_canary_dir, sizeof(flatpak_canary_dir), "%s/app/com.discordapp.DiscordCanary", run_user_dir);
    snprintf(snap_stable_dir, sizeof(snap_stable_dir), "%s/snap.discord", run_user_dir);
    snprintf(snap_ptb_dir, sizeof(snap_ptb_dir), "%s/snap.discord-ptb", run_user_dir);
    snprintf(snap_canary_dir, sizeof(snap_canary_dir), "%s/snap.discord-canary", run_user_dir);

    explicit_ipc_path = getenv("DISCORD_IPC_PATH");
    if (explicit_ipc_path && explicit_ipc_path[0])
    {
        if (strstr(explicit_ipc_path, "discord-ipc-") != NULL)
        {
            snprintf(explicit_socket_path, sizeof(explicit_socket_path), "%s", explicit_ipc_path);
            explicit_socket_dir[0] = '\0';
        }
        else
        {
            snprintf(explicit_socket_dir, sizeof(explicit_socket_dir), "%s", explicit_ipc_path);
            snprintf(explicit_socket_path, sizeof(explicit_socket_path), "%s/discord-ipc-%d", explicit_socket_dir, index);
        }
    }
    else
    {
        explicit_socket_path[0] = '\0';
        explicit_socket_dir[0] = '\0';
    }

    dirs[0] = explicit_socket_dir[0] ? explicit_socket_dir : NULL;
    dirs[1] = getenv("XDG_RUNTIME_DIR");
    dirs[2] = run_user_dir;
    dirs[3] = flatpak_stable_dir;
    dirs[4] = flatpak_ptb_dir;
    dirs[5] = flatpak_canary_dir;
    dirs[6] = snap_stable_dir;
    dirs[7] = snap_ptb_dir;
    dirs[8] = snap_canary_dir;
    dirs[9] = getenv("TMPDIR");
    dirs[10] = getenv("TEMP");
    dirs[11] = getenv("TMP");
    dirs[12] = "/tmp";
    dirs[13] = NULL;

    if (explicit_socket_path[0])
    {
        struct sockaddr_un addr;
        int fd;

        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0)
        {
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", explicit_socket_path);

            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
                return fd;

            close(fd);
        }
    }

    for (d = 0; dirs[d]; ++d)
    {
        struct sockaddr_un addr;
        int fd;
        char path[108];

        if (!dirs[d] || !dirs[d][0])
            continue;

        snprintf(path, sizeof(path), "%s/discord-ipc-%d", dirs[d], index);

        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            continue;

        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            return fd;

        close(fd);
    }

    return -1;
}

static HANDLE CreateFileA(const char *name,
                          DWORD desired_access,
                          DWORD share_mode,
                          void *security,
                          DWORD creation_disposition,
                          DWORD flags,
                          void *template_file)
{
    (void)desired_access;
    (void)share_mode;
    (void)security;
    (void)creation_disposition;
    (void)flags;
    (void)template_file;
    return open_discord_unix_socket_from_pipe_name(name);
}

static int CloseHandle(HANDLE h)
{
    return close(h);
}

static int WriteFile(HANDLE h, const void *buf, DWORD len, DWORD *written, void *overlapped)
{
    ssize_t n;
    (void)overlapped;

    n = write(h, buf, (size_t)len);
    if (written)
        *written = (n > 0) ? (DWORD)n : 0;
    return n == (ssize_t)len;
}

static int ReadFile(HANDLE h, void *buf, DWORD len, DWORD *read_out, void *overlapped)
{
    ssize_t n;
    (void)overlapped;

    n = read(h, buf, (size_t)len);
    if (read_out)
        *read_out = (n > 0) ? (DWORD)n : 0;
    return n == (ssize_t)len;
}

static int PeekNamedPipe(HANDLE h,
                         void *buffer,
                         DWORD nbuffer,
                         DWORD *bytes_read,
                         DWORD *bytes_available,
                         DWORD *bytes_left)
{
    int available = 0;
    (void)buffer;
    (void)nbuffer;
    (void)bytes_read;
    (void)bytes_left;

    if (ioctl(h, FIONREAD, &available) != 0)
        return 0;

    if (bytes_available)
        *bytes_available = (DWORD)((available > 0) ? available : 0);
    return 1;
}

static unsigned long GetCurrentProcessId(void)
{
    return (unsigned long)getpid();
}
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

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
static char s_client_secret[128];
static gamestate_t s_last_gamestate = (gamestate_t)-1;
static int s_last_gameepisode = -1;
static int s_last_gamemap = -1;
static int s_last_netgame = -1;
static int s_last_deathmatch = -1;
static char s_join_secret[128];
static char s_party_id[128];
static int s_party_size;
static int s_party_max;
static char s_presence_mode[32];
static char s_presence_content[96];
static unsigned int s_presence_serial;
static unsigned int s_last_presence_serial;

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

static const char *resolve_optional_arg(const char *name)
{
    int p = M_CheckParm((char *)name);
    if (p && p + 1 < myargc)
        return myargv[p + 1];
    return NULL;
}

static int parse_optional_int(const char *name, int fallback)
{
    const char *v = resolve_optional_arg(name);
    int parsed;

    if (!v || !v[0])
        return fallback;

    parsed = atoi(v);
    if (parsed < 0)
        return fallback;

    return parsed;
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
    char handshake[320];

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

    if (s_client_secret[0])
    {
        snprintf(handshake,
                 sizeof(handshake),
                 "{\"v\":1,\"client_id\":\"%s\",\"client_secret\":\"%s\"}",
                 s_app_id,
                 s_client_secret);
    }
    else
    {
        snprintf(handshake, sizeof(handshake), "{\"v\":1,\"client_id\":\"%s\"}", s_app_id);
    }
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

static int contains_nocase(const char *text, const char *needle)
{
    size_t i;
    size_t j;
    size_t needle_len;

    if (!text || !needle || !needle[0])
        return 0;

    needle_len = strlen(needle);
    for (i = 0; text[i]; ++i)
    {
        for (j = 0; j < needle_len; ++j)
        {
            unsigned char tc = (unsigned char)text[i + j];
            unsigned char nc = (unsigned char)needle[j];

            if (!tc)
                return 0;

            if (tolower(tc) != tolower(nc))
                break;
        }

        if (j == needle_len)
            return 1;
    }

    return 0;
}

static void basename_without_ext(char *dst, size_t dst_size, const char *path)
{
    const char *base;
    const char *slash;
    const char *dot;
    size_t len;
    size_t i;

    if (!dst_size)
        return;

    dst[0] = '\0';
    if (!path || !path[0])
        return;

    base = path;
    slash = strrchr(path, '/');
    if (slash && slash[1])
        base = slash + 1;
    slash = strrchr(base, '\\');
    if (slash && slash[1])
        base = slash + 1;

    dot = strrchr(base, '.');
    len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
    if (len >= dst_size)
        len = dst_size - 1;

    memcpy(dst, base, len);
    dst[len] = '\0';

    for (i = 0; dst[i]; ++i)
    {
        if (dst[i] == '_' || dst[i] == '-')
            dst[i] = ' ';
    }
}

static void get_mode_label(char *mode, size_t mode_size)
{
    if (deathmatch)
        snprintf(mode, mode_size, "Deathmatch");
    else if (netgame)
        snprintf(mode, mode_size, "Co-op");
    else
        snprintf(mode, mode_size, "Single Player");
}

static void get_wad_title(char *wad_title, size_t wad_title_size)
{
    int i;
    const char *source = NULL;

    if (s_presence_content[0])
    {
        basename_without_ext(wad_title, wad_title_size, s_presence_content);
        if (wad_title[0])
            return;
    }

    i = M_CheckParm("-file");
    if (i)
    {
        int j;
        for (j = i + 1; j < myargc; ++j)
        {
            if (myargv[j][0] == '-')
                break;
            source = myargv[j];
            break;
        }
    }

    if (!source)
    {
        i = M_CheckParm("-iwad");
        if (i && i + 1 < myargc)
            source = myargv[i + 1];
    }

    if (source)
    {
        basename_without_ext(wad_title, wad_title_size, source);
        if (wad_title[0])
            return;
    }

    switch (gamemission)
    {
    case pack_plut:
        snprintf(wad_title, wad_title_size, "Plutonia");
        break;
    case pack_tnt:
        snprintf(wad_title, wad_title_size, "TNT Evilution");
        break;
    case doom2:
        snprintf(wad_title, wad_title_size, "DOOM II");
        break;
    default:
        if (gamemode == retail)
            snprintf(wad_title, wad_title_size, "The Ultimate DOOM");
        else if (gamemode == registered)
            snprintf(wad_title, wad_title_size, "DOOM Registered");
        else if (gamemode == shareware)
            snprintf(wad_title, wad_title_size, "DOOM Shareware");
        else
            snprintf(wad_title, wad_title_size, "DOOM");
        break;
    }
}

static const char *get_large_image_key(const char *wad_title)
{
    if (contains_nocase(wad_title, "plutonia"))
        return "plutonia";
    if (contains_nocase(wad_title, "tnt"))
        return "tnt";
    if (contains_nocase(wad_title, "doom2") || contains_nocase(wad_title, "doom ii"))
        return "doom2";
    if (contains_nocase(wad_title, "sigil"))
        return "sigil";
    if (modifiedgame)
        return "pwad";
    return "doomguy";
}

static const char *get_small_image_key(void)
{
    if (s_presence_mode[0])
    {
        if (contains_nocase(s_presence_mode, "deathmatch"))
            return "dm";
        if (contains_nocase(s_presence_mode, "coop") || contains_nocase(s_presence_mode, "co-op"))
            return "coop";
    }

    if (deathmatch)
        return "dm";
    if (netgame)
        return "coop";
    return "single";
}

static void get_presence_strings(char *details, size_t details_size,
                                 char *state, size_t state_size)
{
    char mode[32];
    char wad_title[64];

    get_mode_label(mode, sizeof(mode));
    get_wad_title(wad_title, sizeof(wad_title));

    if (gamestate == GS_LEVEL)
    {
        if (gamemode == commercial)
            snprintf(details, details_size, "MAP%02d", gamemap);
        else
            snprintf(details, details_size, "E%dM%d", gameepisode, gamemap);

        if (wad_title[0])
            snprintf(state, state_size, "%s - %s", mode, wad_title);
        else
            snprintf(state, state_size, "%s", mode);

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

    if (wad_title[0])
        snprintf(state, state_size, "Idle - %s", wad_title);
    else
        snprintf(state, state_size, "Idle");
}

static int send_activity_update(void)
{
    char details[64];
    char state[64];
    char wad_title[64];
    char details_json[128];
    char state_json[128];
    char wad_title_json[128];
    char mode_json[64];
    char payload[768];
    char nonce[32];
    const char *large_image;
    const char *small_image;
    char mode_label[32];
    int wrote;
    char party_block[256];
    char join_block[192];

    party_block[0] = '\0';
    join_block[0] = '\0';

    if (s_party_id[0] && s_party_max > 0)
    {
        int effective_size = s_party_size;
        if (effective_size < 0)
            effective_size = 0;
        if (effective_size > s_party_max)
            effective_size = s_party_max;

        snprintf(party_block,
                 sizeof(party_block),
                 ",\"party\":{\"id\":\"%s\",\"size\":[%d,%d]}",
                 s_party_id,
                 effective_size,
                 s_party_max);
    }

    if (s_join_secret[0])
    {
        snprintf(join_block,
                 sizeof(join_block),
                 ",\"secrets\":{\"join\":\"%s\"}",
                 s_join_secret);
    }

    get_presence_strings(details, sizeof(details), state, sizeof(state));
    get_wad_title(wad_title, sizeof(wad_title));
    get_mode_label(mode_label, sizeof(mode_label));
    large_image = get_large_image_key(wad_title);
    small_image = get_small_image_key();
    json_escape(details_json, sizeof(details_json), details);
    json_escape(state_json, sizeof(state_json), state);
    json_escape(wad_title_json, sizeof(wad_title_json), wad_title);
    json_escape(mode_json, sizeof(mode_json), mode_label);

    snprintf(nonce, sizeof(nonce), "%u", ++s_nonce);

    if (gamestate == GS_LEVEL)
    {
        long long started = (long long)time(NULL) - (long long)(leveltime / TICRATE);
        wrote = snprintf(payload,
                         sizeof(payload),
                         "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":{\"state\":\"%s\",\"details\":\"%s\",\"timestamps\":{\"start\":%lld},\"assets\":{\"large_image\":\"%s\",\"large_text\":\"%s\",\"small_image\":\"%s\",\"small_text\":\"%s\"}%s%s}},\"nonce\":\"%s\"}",
                         (unsigned long)GetCurrentProcessId(),
                         state_json,
                         details_json,
                         started,
                         large_image,
                         wad_title_json,
                         small_image,
                         mode_json,
                         party_block,
                         join_block,
                         nonce);
    }
    else
    {
        wrote = snprintf(payload,
                         sizeof(payload),
                         "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":{\"state\":\"%s\",\"details\":\"%s\",\"assets\":{\"large_image\":\"%s\",\"large_text\":\"%s\",\"small_image\":\"%s\",\"small_text\":\"%s\"}%s%s}},\"nonce\":\"%s\"}",
                         (unsigned long)GetCurrentProcessId(),
                         state_json,
                         details_json,
                         large_image,
                         wad_title_json,
                         small_image,
                         mode_json,
                         party_block,
                         join_block,
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
    const char *client_secret;
    const char *join_secret;
    const char *party_id;

    if (s_discord_enabled)
        return;

    app_id = resolve_discord_app_id();
    if (!app_id || !app_id[0])
    {
        printf("Discord RPC: disabled (set -discordappid <id> or DISCORD_APP_ID).\n");
        return;
    }

    snprintf(s_app_id, sizeof(s_app_id), "%s", app_id);

    join_secret = resolve_optional_arg("-discordjoinsecret");
    client_secret = resolve_optional_arg("-discordclientsecret");
    if (!client_secret)
        client_secret = getenv("DISCORD_CLIENT_SECRET");
    party_id = resolve_optional_arg("-discordpartyid");

    s_join_secret[0] = '\0';
    s_client_secret[0] = '\0';
    s_party_id[0] = '\0';
    if (join_secret)
        snprintf(s_join_secret, sizeof(s_join_secret), "%s", join_secret);
    if (client_secret)
        snprintf(s_client_secret, sizeof(s_client_secret), "%s", client_secret);
    if (party_id)
        snprintf(s_party_id, sizeof(s_party_id), "%s", party_id);

    s_party_size = parse_optional_int("-discordpartysize", netgame ? 2 : 1);
    s_party_max = parse_optional_int("-discordpartymax", 4);

    s_discord_enabled = 1;
    s_last_update_tic = -TICRATE * 10;
    s_last_connect_attempt_tic = -TICRATE * 10;

    if (connect_and_handshake())
        printf("Discord RPC: connected (IPC) with app id %s (deathmatch=%d, netgame=%d)\n", s_app_id, deathmatch, netgame);
    else
        printf("Discord RPC: waiting for Discord client (IPC). Will notify when deathmatch/mode changes.\n");
}

void I_DiscordRPC_Update(void)
{
    int now_tic;
    int should_update;
    int state_changed;

    if (!s_discord_enabled)
        return;

    now_tic = I_GetTime();

    // Check if any game state has changed
    state_changed = 0;
    if (gamestate != s_last_gamestate
        || gameepisode != s_last_gameepisode
        || gamemap != s_last_gamemap
        || netgame != s_last_netgame
        || deathmatch != s_last_deathmatch
        || s_presence_serial != s_last_presence_serial)
    {
        state_changed = 1;
    }

    if (!s_discord_connected)
    {
        // Try more frequently to connect when state changes (important for mode notifications)
        int reconnect_interval = state_changed ? TICRATE : TICRATE * 5;
        
        if (now_tic - s_last_connect_attempt_tic >= reconnect_interval)
        {
            s_last_connect_attempt_tic = now_tic;
            connect_and_handshake();
        }

        if (!s_discord_connected)
            return;
    }

    drain_incoming_frames();

    should_update = 0;
    if (state_changed)
    {
        should_update = 1;
        if (deathmatch != s_last_deathmatch || netgame != s_last_netgame)
            printf("Discord RPC: state changed (deathmatch: %d->%d, netgame: %d->%d)\n", 
                   s_last_deathmatch, deathmatch, s_last_netgame, netgame);
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
    s_last_presence_serial = s_presence_serial;
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

void I_DiscordRPC_SetSteamJoin(unsigned long long lobby_id,
                               int party_size,
                               int party_max,
                               const char *mode,
                               const char *content)
{
    if (!lobby_id)
    {
        s_join_secret[0] = '\0';
        s_party_id[0] = '\0';
        s_presence_mode[0] = '\0';
        s_presence_content[0] = '\0';
        ++s_presence_serial;
        return;
    }

    if (party_size < 1)
        party_size = 1;
    if (party_max < party_size)
        party_max = party_size;

    snprintf(s_join_secret, sizeof(s_join_secret), "steam:%llu", lobby_id);
    snprintf(s_party_id, sizeof(s_party_id), "steam-%llu", lobby_id);
    if (mode && mode[0])
        snprintf(s_presence_mode, sizeof(s_presence_mode), "%s", mode);
    else
        s_presence_mode[0] = '\0';
    if (content && content[0])
        snprintf(s_presence_content, sizeof(s_presence_content), "%s", content);
    else
        s_presence_content[0] = '\0';
    s_party_size = party_size;
    s_party_max = party_max;
    ++s_presence_serial;

    /* Force a presence refresh on next update tick. */
    s_last_gamestate = (gamestate_t)-1;
    s_last_netgame = -1;
    s_last_deathmatch = -1;
    s_last_update_tic = -TICRATE * 10;
}
