
//----------------------------------------------------------
//            DOOM'93 Win32 sound system
//
//  WaveOut audio backend with OGG support and stb_vorbis
//
//  veeλnti is responsible for this
//----------------------------------------------------------

#define boolean windows_boolean_workaround
#include <windows.h>
#include <mmsystem.h>
#undef boolean

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "i_sound.h"
#include "i_system.h"
#include "m_argv.h"
#include "ogg_temp.h"
#include "w_wad.h"
#include "z_zone.h"

#define SAMPLECOUNT 512
#define NUM_CHANNELS 8
#define SAMPLERATE 11025
#define BUFFER_SAMPLES (SAMPLECOUNT * 2)
#define MIXBUFFER_BYTES (SAMPLECOUNT * 4)
#define WAVE_BUFFER_COUNT 4
#define MAX_REGISTERED_SONGS 8
#define MIDI_DIVISION 70
#define MUSIC_ALIAS "doommusic"

static int lengths[NUMSFX];
static signed short mixbuffer[BUFFER_SAMPLES];

static unsigned int channelstep[NUM_CHANNELS];
static unsigned int channelstepremainder[NUM_CHANNELS];
static unsigned char *channels[NUM_CHANNELS];
static unsigned char *channelsend[NUM_CHANNELS];
static int channelstart[NUM_CHANNELS];
static int channelhandles[NUM_CHANNELS];
static int channelids[NUM_CHANNELS];

static int steptable[256];
static int vol_lookup[128 * 256];
static int *channelleftvol_lookup[NUM_CHANNELS];
static int *channelrightvol_lookup[NUM_CHANNELS];

static HWAVEOUT s_waveout;
static WAVEHDR s_waveheaders[WAVE_BUFFER_COUNT];
static unsigned char s_wavebuffers[WAVE_BUFFER_COUNT][MIXBUFFER_BYTES];
static int s_waveprepared[WAVE_BUFFER_COUNT];
static int s_nextbuffer;
static int s_soundready;

typedef enum
{
    SONG_TYPE_NONE,
    SONG_TYPE_MIDI,
    SONG_TYPE_DIGITAL
} songtype_t;

typedef struct
{
    int in_use;
    int temporary;
    songtype_t type;
    char path[MAX_PATH];
} songslot_t;

typedef struct
{
    unsigned char *data;
    size_t size;
    size_t capacity;
} bytebuffer_t;

static songslot_t s_songslots[MAX_REGISTERED_SONGS];
static int s_playing_song;
static int s_musicpaused;
static char s_music_name[16];
static int s_music_lumpnum = -1;
static int s_music_lumplength;
static char s_music_error[128] = "unknown music error";

#ifdef SNDSERV
FILE *sndserver = NULL;
char *sndserver_filename = "sndserver";
#endif

static unsigned short read_u16le(const unsigned char *data)
{
    return (unsigned short)(data[0] | (data[1] << 8));
}

static void set_music_error(const char *message)
{
    if (!message)
        message = "unknown music error";

    strncpy(s_music_error, message, sizeof(s_music_error) - 1);
    s_music_error[sizeof(s_music_error) - 1] = '\0';
}

static void copy_string(char *dest, size_t dest_size, const char *src)
{
    if (!dest || !dest_size)
        return;

    if (!src)
    {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static int file_exists(const char *path)
{
    DWORD attributes;

    attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES
        && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static void path_remove_filename(char *path)
{
    char *slash;
    char *alt;

    slash = strrchr(path, '\\');
    alt = strrchr(path, '/');
    if (alt && (!slash || alt > slash))
        slash = alt;

    if (slash)
        *slash = '\0';
    else
        copy_string(path, MAX_PATH, ".");
}

static void path_join(char *out, size_t out_size, const char *dir, const char *name)
{
    size_t dir_len;

    if (!dir || !dir[0])
    {
        snprintf(out, out_size, "%s", name);
        return;
    }

    dir_len = strlen(dir);
    if (dir[dir_len - 1] == '\\' || dir[dir_len - 1] == '/')
        snprintf(out, out_size, "%s%s", dir, name);
    else
        snprintf(out, out_size, "%s\\%s", dir, name);
}

static int same_path(const char *left, const char *right)
{
    return _stricmp(left, right) == 0;
}

static int buffer_reserve(bytebuffer_t *buffer, size_t extra)
{
    unsigned char *grown;
    size_t needed;
    size_t capacity;

    needed = buffer->size + extra;
    if (needed <= buffer->capacity)
        return 1;

    capacity = buffer->capacity ? buffer->capacity : 1024;
    while (capacity < needed)
        capacity *= 2;

    grown = (unsigned char *)realloc(buffer->data, capacity);
    if (!grown)
        return 0;

    buffer->data = grown;
    buffer->capacity = capacity;
    return 1;
}

static int buffer_write_byte(bytebuffer_t *buffer, unsigned char value)
{
    if (!buffer_reserve(buffer, 1))
        return 0;

    buffer->data[buffer->size++] = value;
    return 1;
}

static int buffer_write_bytes(bytebuffer_t *buffer, const void *data, size_t length)
{
    if (!buffer_reserve(buffer, length))
        return 0;

    memcpy(buffer->data + buffer->size, data, length);
    buffer->size += length;
    return 1;
}

static int buffer_write_u16be(bytebuffer_t *buffer, unsigned short value)
{
    return buffer_write_byte(buffer, (unsigned char)((value >> 8) & 0xff))
        && buffer_write_byte(buffer, (unsigned char)(value & 0xff));
}

static int buffer_write_u32be(bytebuffer_t *buffer, unsigned int value)
{
    return buffer_write_byte(buffer, (unsigned char)((value >> 24) & 0xff))
        && buffer_write_byte(buffer, (unsigned char)((value >> 16) & 0xff))
        && buffer_write_byte(buffer, (unsigned char)((value >> 8) & 0xff))
        && buffer_write_byte(buffer, (unsigned char)(value & 0xff));
}

static int buffer_write_vlq(bytebuffer_t *buffer, unsigned int value)
{
    unsigned char scratch[5];
    int count;

    count = 0;
    scratch[count++] = (unsigned char)(value & 0x7f);
    value >>= 7;

    while (value)
    {
        scratch[count++] = (unsigned char)((value & 0x7f) | 0x80);
        value >>= 7;
    }

    while (count--)
        if (!buffer_write_byte(buffer, scratch[count]))
            return 0;

    return 1;
}

static int write_midi_event3(bytebuffer_t *track, unsigned int delta, unsigned char status,
    unsigned char data1, unsigned char data2)
{
    return buffer_write_vlq(track, delta)
        && buffer_write_byte(track, status)
        && buffer_write_byte(track, data1)
        && buffer_write_byte(track, data2);
}

static int write_midi_event2(bytebuffer_t *track, unsigned int delta, unsigned char status,
    unsigned char data1)
{
    return buffer_write_vlq(track, delta)
        && buffer_write_byte(track, status)
        && buffer_write_byte(track, data1);
}

static int map_mus_channel(int mus_channel, int channel_map[16], int *next_channel)
{
    if (mus_channel == 15)
        return 9;

    if (channel_map[mus_channel] >= 0)
        return channel_map[mus_channel];

    while (*next_channel == 9)
        (*next_channel)++;

    channel_map[mus_channel] = *next_channel;
    (*next_channel)++;
    return channel_map[mus_channel];
}

static int mus_to_midi(const unsigned char *musdata, int muslen, bytebuffer_t *midi)
{
    static const unsigned char controller_map[] =
    {
        0x00, 0x20, 0x01, 0x07, 0x0a,
        0x0b, 0x5b, 0x5d, 0x40, 0x43,
        0x78, 0x7b, 0x7e, 0x7f, 0x79
    };
    bytebuffer_t track;
    unsigned int queued_delta;
    int channel_map[16];
    unsigned char last_volume[16];
    unsigned short score_len;
    unsigned short score_start;
    unsigned int score_end;
    int next_channel;
    int pos;
    int i;

    memset(&track, 0, sizeof(track));
    set_music_error("invalid MUS data");

    if (!musdata || muslen < 16 || memcmp(musdata, "MUS\x1a", 4) != 0)
        return 0;

    score_len = read_u16le(musdata + 4);
    score_start = read_u16le(musdata + 6);
    score_end = score_start + score_len;
    if (score_start >= (unsigned int)muslen || score_end > (unsigned int)muslen)
    {
        set_music_error("MUS header has an invalid score range");
        return 0;
    }

    for (i = 0; i < 16; i++)
    {
        channel_map[i] = -1;
        last_volume[i] = 64;
    }

    next_channel = 0;
    queued_delta = 0;
    pos = score_start;

    if (!buffer_write_vlq(&track, 0)
        || !buffer_write_byte(&track, 0xff)
        || !buffer_write_byte(&track, 0x51)
        || !buffer_write_byte(&track, 0x03)
        || !buffer_write_byte(&track, 0x07)
        || !buffer_write_byte(&track, 0xa1)
        || !buffer_write_byte(&track, 0x20))
    {
        set_music_error("out of memory while writing MIDI tempo event");
        goto fail;
    }

    while (pos < (int)score_end)
    {
        unsigned char descriptor;
        int mus_channel;
        int midi_channel;
        int event_type;

        descriptor = musdata[pos++];
        mus_channel = descriptor & 0x0f;
        event_type = (descriptor >> 4) & 0x07;
        midi_channel = map_mus_channel(mus_channel, channel_map, &next_channel);

        switch (event_type)
        {
        case 0:
        {
            unsigned char note;

            if (pos >= (int)score_end)
            {
                set_music_error("truncated MUS release-note event");
                goto fail;
            }

            note = (unsigned char)(musdata[pos++] & 0x7f);
            if (!write_midi_event3(&track, queued_delta, (unsigned char)(0x90 | midi_channel), note, 0))
            {
                set_music_error("out of memory while writing MIDI release-note event");
                goto fail;
            }
            queued_delta = 0;
            break;
        }
        case 1:
        {
            unsigned char note;

            if (pos >= (int)score_end)
            {
                set_music_error("truncated MUS play-note event");
                goto fail;
            }

            note = musdata[pos++];
            if (note & 0x80)
            {
                if (pos >= (int)score_end)
                {
                    set_music_error("truncated MUS note volume event");
                    goto fail;
                }
                last_volume[mus_channel] = musdata[pos++];
            }

            note &= 0x7f;
            if (!write_midi_event3(&track, queued_delta, (unsigned char)(0x90 | midi_channel),
                note, last_volume[mus_channel]))
            {
                set_music_error("out of memory while writing MIDI note-on event");
                goto fail;
            }
            queued_delta = 0;
            break;
        }
        case 2:
        {
            unsigned short bend;
            unsigned char wheel;

            if (pos >= (int)score_end)
            {
                set_music_error("truncated MUS pitch event");
                goto fail;
            }

            wheel = musdata[pos++];
            bend = (unsigned short)(wheel << 6);
            if (!write_midi_event3(&track, queued_delta, (unsigned char)(0xe0 | midi_channel),
                (unsigned char)(bend & 0x7f),
                (unsigned char)((bend >> 7) & 0x7f)))
            {
                set_music_error("out of memory while writing MIDI pitch-wheel event");
                goto fail;
            }
            queued_delta = 0;
            break;
        }
        case 3:
        {
            unsigned char controller;

            if (pos >= (int)score_end)
            {
                set_music_error("truncated MUS system event");
                goto fail;
            }

            controller = musdata[pos++];
            if (controller < 10 || controller > 14)
                break;

            if (!write_midi_event3(&track, queued_delta, (unsigned char)(0xb0 | midi_channel),
                controller_map[controller], 0))
            {
                set_music_error("out of memory while writing MIDI controller event");
                goto fail;
            }
            queued_delta = 0;
            break;
        }
        case 4:
        {
            unsigned char controller;
            unsigned char value;

            if (pos + 1 >= (int)score_end)
            {
                set_music_error("truncated MUS controller-change event");
                goto fail;
            }

            controller = musdata[pos++];
            value = musdata[pos++];

            if (!controller)
            {
                if (!write_midi_event2(&track, queued_delta, (unsigned char)(0xc0 | midi_channel), value))
                {
                    set_music_error("out of memory while writing MIDI program-change event");
                    goto fail;
                }
            }
            else
            {
                if (controller > 9)
                    break;
                if (!write_midi_event3(&track, queued_delta, (unsigned char)(0xb0 | midi_channel),
                    controller_map[controller], value))
                {
                    set_music_error("out of memory while writing MIDI control-change event");
                    goto fail;
                }
            }

            queued_delta = 0;
            break;
        }
        case 5:
            break;
        case 6:
            pos = score_end;
            break;
        case 7:
            if (pos >= (int)score_end)
            {
                set_music_error("truncated unused MUS event");
                goto fail;
            }
            pos++;
            break;
        default:
            set_music_error("unsupported MUS event type");
            goto fail;
        }

        if (descriptor & 0x80)
        {
            unsigned int delay;
            unsigned char time_byte;

            delay = 0;
            do
            {
                if (pos >= (int)score_end)
                {
                    set_music_error("truncated MUS time delay");
                    goto fail;
                }

                time_byte = musdata[pos++];
                delay = (delay << 7) | (time_byte & 0x7f);
            } while (time_byte & 0x80);

            queued_delta += delay;
        }
    }

    if (!buffer_write_vlq(&track, queued_delta)
        || !buffer_write_byte(&track, 0xff)
        || !buffer_write_byte(&track, 0x2f)
        || !buffer_write_byte(&track, 0x00))
    {
        set_music_error("out of memory while writing MIDI track end");
        goto fail;
    }

    if (!buffer_write_bytes(midi, "MThd", 4)
        || !buffer_write_u32be(midi, 6)
        || !buffer_write_u16be(midi, 0)
        || !buffer_write_u16be(midi, 1)
        || !buffer_write_u16be(midi, MIDI_DIVISION)
        || !buffer_write_bytes(midi, "MTrk", 4)
        || !buffer_write_u32be(midi, (unsigned int)track.size)
        || !buffer_write_bytes(midi, track.data, track.size))
    {
        set_music_error("out of memory while assembling MIDI file");
        goto fail;
    }

    free(track.data);
    return 1;

fail:
    free(track.data);
    return 0;
}

static int write_file(const char *path, const unsigned char *data, size_t size)
{
    FILE *stream;

    stream = fopen(path, "wb");
    if (!stream)
        return 0;

    if (fwrite(data, 1, size, stream) != size)
    {
        fclose(stream);
        remove(path);
        return 0;
    }

    fclose(stream);
    return 1;
}

static int create_temp_midi_path(char *path, size_t path_size)
{
    char temp_dir[MAX_PATH];
    char temp_file[MAX_PATH];

    if (!GetTempPathA(MAX_PATH, temp_dir))
        return 0;

    if (!GetTempFileNameA(temp_dir, "dmu", 0, temp_file))
        return 0;

    remove(temp_file);
    snprintf(path, path_size, "%s.mid", temp_file);
    return 1;
}

static int create_temp_music_path(const char *extension, char *path, size_t path_size)
{
    char temp_dir[MAX_PATH];
    char temp_file[MAX_PATH];

    if (!extension || !extension[0])
        return 0;

    if (!GetTempPathA(MAX_PATH, temp_dir))
        return 0;

    if (!GetTempFileNameA(temp_dir, "dmu", 0, temp_file))
        return 0;

    remove(temp_file);
    snprintf(path, path_size, "%s%s", temp_file, extension);
    return 1;
}

static int create_temp_music_file(const unsigned char *musicdata, int musiclen,
    const char *extension, char *out_path, size_t out_path_size)
{
    if (!musicdata || musiclen <= 0)
    {
        set_music_error("missing music data");
        return 0;
    }

    if (!create_temp_music_path(extension, out_path, out_path_size))
    {
        set_music_error("failed to create a temporary music path");
        return 0;
    }

    if (!write_file(out_path, musicdata, (size_t)musiclen))
    {
        set_music_error("failed to write temporary music file");
        return 0;
    }

    return 1;
}

static int create_temp_midi_file(const unsigned char *musdata, int muslen, char *out_path, size_t out_path_size)
{
    bytebuffer_t midi;
    int success;

    memset(&midi, 0, sizeof(midi));

    if (!create_temp_midi_path(out_path, out_path_size))
    {
        set_music_error("failed to create a temporary MIDI path");
        return 0;
    }

    if (!mus_to_midi(musdata, muslen, &midi))
    {
        free(midi.data);
        return 0;
    }

    success = write_file(out_path, midi.data, midi.size);
    free(midi.data);
    if (!success)
        set_music_error("failed to write temporary MIDI file");
    return success;
}

static void report_mci_error(const char *context, MCIERROR error)
{
    char message[256];

    if (!error)
        return;

    if (!mciGetErrorStringA(error, message, sizeof(message)))
        strcpy(message, "unknown MCI error");

    fprintf(stderr, "%s: %s\n", context, message);
}

static void close_current_music(void)
{
    mciSendStringA("stop " MUSIC_ALIAS, NULL, 0, NULL);
    mciSendStringA("close " MUSIC_ALIAS, NULL, 0, NULL);
    s_playing_song = 0;
    s_musicpaused = 0;
}

static void apply_music_volume(void)
{
    unsigned int volume16;
    DWORD packed_volume;
    char command[128];

    if (snd_MusicVolume < 0)
        snd_MusicVolume = 0;
    else if (snd_MusicVolume > 15)
        snd_MusicVolume = 15;

    volume16 = (unsigned int)((snd_MusicVolume * 0xffffu) / 15u);
    packed_volume = volume16 | (volume16 << 16);
    midiOutSetVolume((HMIDIOUT)(DWORD_PTR)MIDI_MAPPER, packed_volume);

    if (!s_playing_song)
        return;

    snprintf(command, sizeof(command), "setaudio %s volume to %d", MUSIC_ALIAS,
        (snd_MusicVolume * 1000) / 15);
    mciSendStringA(command, NULL, 0, NULL);
}

static int open_music_alias(const songslot_t *slot)
{
    MCIERROR error;
    char command[MAX_PATH * 2 + 64];

    close_current_music();

    if (slot->type == SONG_TYPE_MIDI)
    {
        snprintf(command, sizeof(command), "open \"%s\" type sequencer alias %s",
            slot->path, MUSIC_ALIAS);
        error = mciSendStringA(command, NULL, 0, NULL);
        if (error)
        {
            report_mci_error("I_PlaySong: MIDI open failed", error);
            return 0;
        }
    }
    else
    {
        snprintf(command, sizeof(command), "open \"%s\" alias %s", slot->path, MUSIC_ALIAS);
        error = mciSendStringA(command, NULL, 0, NULL);
        if (error)
        {
            snprintf(command, sizeof(command), "open \"%s\" type mpegvideo alias %s",
                slot->path, MUSIC_ALIAS);
            error = mciSendStringA(command, NULL, 0, NULL);
            if (error)
            {
                report_mci_error("I_PlaySong: digital music open failed", error);
                return 0;
            }
        }
    }

    return 1;
}

static int allocate_song_slot(void)
{
    int i;

    for (i = 0; i < MAX_REGISTERED_SONGS; i++)
        if (!s_songslots[i].in_use)
            return i;

    return -1;
}

static int external_music_candidate(const char *dir, const char *stem, char *out_path, size_t out_path_size)
{
    static const char *extensions[] = { ".ogg", ".mp3", ".mid", ".midi" };
    char candidate[MAX_PATH];
    int i;

    for (i = 0; i < (int)(sizeof(extensions) / sizeof(extensions[0])); i++)
    {
        snprintf(candidate, sizeof(candidate), "%s%s", stem, extensions[i]);
        path_join(out_path, out_path_size, dir, candidate);
        if (file_exists(out_path))
            return 1;
    }

    return 0;
}

static int find_external_music_file(char *out_path, size_t out_path_size, songtype_t *type)
{
    char exe_dir[MAX_PATH];
    char search_dirs[6][MAX_PATH];
    char prefixed[24];
    int dir_count;
    int i;

    if (!s_music_name[0])
        return 0;

    if (!GetModuleFileNameA(NULL, exe_dir, MAX_PATH))
        copy_string(exe_dir, sizeof(exe_dir), ".");

    path_remove_filename(exe_dir);

    dir_count = 0;
    copy_string(search_dirs[dir_count++], MAX_PATH, exe_dir);
    copy_string(search_dirs[dir_count], MAX_PATH, exe_dir);
    path_remove_filename(search_dirs[dir_count++]);
    copy_string(search_dirs[dir_count], MAX_PATH, search_dirs[dir_count - 1]);
    path_remove_filename(search_dirs[dir_count++]);
    copy_string(search_dirs[dir_count++], MAX_PATH, ".");

    snprintf(prefixed, sizeof(prefixed), "d_%s", s_music_name);

    for (i = 0; i < dir_count; i++)
    {
        char music_dir[MAX_PATH];

        if (external_music_candidate(search_dirs[i], s_music_name, out_path, out_path_size)
            || external_music_candidate(search_dirs[i], prefixed, out_path, out_path_size))
            goto found;

        path_join(music_dir, sizeof(music_dir), search_dirs[i], "music");
        if (external_music_candidate(music_dir, s_music_name, out_path, out_path_size)
            || external_music_candidate(music_dir, prefixed, out_path, out_path_size))
            goto found;
    }

    return 0;

found:
    if (_stricmp(strrchr(out_path, '.'), ".mid") == 0
        || _stricmp(strrchr(out_path, '.'), ".midi") == 0)
        *type = SONG_TYPE_MIDI;
    else
        *type = SONG_TYPE_DIGITAL;
    return 1;
}

static void release_song_slot(int index)
{
    if (index < 0 || index >= MAX_REGISTERED_SONGS || !s_songslots[index].in_use)
        return;

    if (s_playing_song == index + 1)
        close_current_music();

    if (s_songslots[index].temporary && s_songslots[index].path[0])
        remove(s_songslots[index].path);

    memset(&s_songslots[index], 0, sizeof(s_songslots[index]));
}

static void *getsfx(char *sfxname, int *len)
{
    unsigned char *sfx;
    unsigned char *paddedsfx;
    int i;
    int size;
    int paddedsize;
    char name[20];
    int sfxlump;

    sprintf(name, "ds%s", sfxname);

    if (W_CheckNumForName(name) == -1)
        sfxlump = W_GetNumForName("dspistol");
    else
        sfxlump = W_GetNumForName(name);

    size = W_LumpLength(sfxlump);
    sfx = (unsigned char *)W_CacheLumpNum(sfxlump, PU_STATIC);

    paddedsize = ((size - 8 + (SAMPLECOUNT - 1)) / SAMPLECOUNT) * SAMPLECOUNT;
    paddedsfx = (unsigned char *)malloc(paddedsize + 8);
    if (!paddedsfx)
        I_Error("getsfx: failed allocation of %d bytes", paddedsize + 8);

    memcpy(paddedsfx, sfx, size);
    for (i = size; i < paddedsize + 8; i++)
        paddedsfx[i] = 128;

    Z_Free(sfx);

    *len = paddedsize;
    return (void *)(paddedsfx + 8);
}

static int addsfx(int sfxid, int volume, int step, int separation)
{
    static unsigned short handlenums = 0;
    int i;
    int oldest;
    int oldestnum;
    int slot;
    int rightvol;
    int leftvol;
    int rc;

    rc = -1;
    oldest = gametic;
    oldestnum = 0;

    if (sfxid == sfx_sawup
        || sfxid == sfx_sawidl
        || sfxid == sfx_sawful
        || sfxid == sfx_sawhit
        || sfxid == sfx_stnmov
        || sfxid == sfx_pistol)
    {
        for (i = 0; i < NUM_CHANNELS; i++)
        {
            if (channels[i] && channelids[i] == sfxid)
            {
                channels[i] = 0;
                break;
            }
        }
    }

    for (i = 0; (i < NUM_CHANNELS) && channels[i]; i++)
    {
        if (channelstart[i] < oldest)
        {
            oldestnum = i;
            oldest = channelstart[i];
        }
    }

    if (i == NUM_CHANNELS)
        slot = oldestnum;
    else
        slot = i;

    channels[slot] = (unsigned char *)S_sfx[sfxid].data;
    channelsend[slot] = channels[slot] + lengths[sfxid];

    if (!handlenums)
        handlenums = 100;

    channelhandles[slot] = rc = handlenums++;
    channelstep[slot] = step;
    channelstepremainder[slot] = 0;
    channelstart[slot] = gametic;

    separation += 1;
    leftvol = volume - ((volume * separation * separation) >> 16);
    separation = separation - 257;
    rightvol = volume - ((volume * separation * separation) >> 16);

    if (rightvol < 0 || rightvol > 127)
        I_Error("rightvol out of bounds");
    if (leftvol < 0 || leftvol > 127)
        I_Error("leftvol out of bounds");

    channelleftvol_lookup[slot] = &vol_lookup[leftvol * 256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol * 256];
    channelids[slot] = sfxid;

    return rc;
}

static void release_completed_buffers(void)
{
    int i;
    MMRESULT result;

    if (!s_waveout)
        return;

    for (i = 0; i < WAVE_BUFFER_COUNT; i++)
    {
        if (s_waveprepared[i] && (s_waveheaders[i].dwFlags & WHDR_DONE))
        {
            result = waveOutUnprepareHeader(s_waveout, &s_waveheaders[i], sizeof(WAVEHDR));
            if (result == MMSYSERR_NOERROR)
                s_waveprepared[i] = 0;
        }
    }
}

void I_SetChannels(void)
{
    int i;
    int j;
    int *steptablemid;

    steptablemid = steptable + 128;

    for (i = 0; i < NUM_CHANNELS; i++)
    {
        channels[i] = 0;
        channelsend[i] = 0;
        channelstep[i] = 0;
        channelstepremainder[i] = 0;
        channelstart[i] = 0;
        channelhandles[i] = 0;
        channelids[i] = 0;
        channelleftvol_lookup[i] = &vol_lookup[0];
        channelrightvol_lookup[i] = &vol_lookup[0];
    }

    for (i = -128; i < 128; i++)
        steptablemid[i] = (int)(pow(2.0, (i / 64.0)) * 65536.0);

    for (i = 0; i < 128; i++)
        for (j = 0; j < 256; j++)
            vol_lookup[i * 256 + j] = (i * (j - 128) * 256) / 127;
}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfxinfo->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    priority = 0;

    if (!s_soundready)
        return 0;

    return addsfx(id, vol, steptable[pitch], sep);
}

void I_StopSound(int handle)
{
    int i;

    for (i = 0; i < NUM_CHANNELS; i++)
    {
        if (channelhandles[i] == handle)
        {
            channels[i] = 0;
            channelhandles[i] = 0;
            return;
        }
    }
}

int I_SoundIsPlaying(int handle)
{
    int i;

    for (i = 0; i < NUM_CHANNELS; i++)
        if (channelhandles[i] == handle && channels[i])
            return 1;

    return 0;
}

void I_UpdateSound(void)
{
    unsigned int sample;
    int dl;
    int dr;
    signed short *leftout;
    signed short *rightout;
    signed short *leftend;
    int step;
    int chan;

    if (!s_soundready)
        return;

    leftout = mixbuffer;
    rightout = mixbuffer + 1;
    step = 2;
    leftend = mixbuffer + SAMPLECOUNT * step;

    while (leftout != leftend)
    {
        dl = 0;
        dr = 0;

        for (chan = 0; chan < NUM_CHANNELS; chan++)
        {
            if (channels[chan])
            {
                sample = *channels[chan];
                dl += channelleftvol_lookup[chan][sample];
                dr += channelrightvol_lookup[chan][sample];
                channelstepremainder[chan] += channelstep[chan];
                channels[chan] += channelstepremainder[chan] >> 16;
                channelstepremainder[chan] &= 65536 - 1;

                if (channels[chan] >= channelsend[chan])
                    channels[chan] = 0;
            }
        }

        if (dl > 0x7fff)
            *leftout = 0x7fff;
        else if (dl < -0x8000)
            *leftout = -0x8000;
        else
            *leftout = (signed short)dl;

        if (dr > 0x7fff)
            *rightout = 0x7fff;
        else if (dr < -0x8000)
            *rightout = -0x8000;
        else
            *rightout = (signed short)dr;

        leftout += step;
        rightout += step;
    }
}

void I_SubmitSound(void)
{
    int attempts;
    int index;
    MMRESULT result;

    if (!s_soundready || !s_waveout)
        return;

    release_completed_buffers();

    for (attempts = 0; attempts < WAVE_BUFFER_COUNT; attempts++)
    {
        index = (s_nextbuffer + attempts) % WAVE_BUFFER_COUNT;
        if (!s_waveprepared[index])
            break;
    }

    if (attempts == WAVE_BUFFER_COUNT)
        return;

    memcpy(s_wavebuffers[index], mixbuffer, MIXBUFFER_BYTES);
    memset(&s_waveheaders[index], 0, sizeof(WAVEHDR));
    s_waveheaders[index].lpData = (LPSTR)s_wavebuffers[index];
    s_waveheaders[index].dwBufferLength = MIXBUFFER_BYTES;

    result = waveOutPrepareHeader(s_waveout, &s_waveheaders[index], sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR)
        return;

    s_waveprepared[index] = 1;
    result = waveOutWrite(s_waveout, &s_waveheaders[index], sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR)
    {
        waveOutUnprepareHeader(s_waveout, &s_waveheaders[index], sizeof(WAVEHDR));
        s_waveprepared[index] = 0;
        return;
    }

    s_nextbuffer = (index + 1) % WAVE_BUFFER_COUNT;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    int i;
    int separation;
    int rightvol;
    int leftvol;

    for (i = 0; i < NUM_CHANNELS; i++)
    {
        if (channelhandles[i] != handle || !channels[i])
            continue;

        separation = sep + 1;
        leftvol = vol - ((vol * separation * separation) >> 16);
        separation = separation - 257;
        rightvol = vol - ((vol * separation * separation) >> 16);

        if (rightvol < 0)
            rightvol = 0;
        else if (rightvol > 127)
            rightvol = 127;

        if (leftvol < 0)
            leftvol = 0;
        else if (leftvol > 127)
            leftvol = 127;

        channelleftvol_lookup[i] = &vol_lookup[leftvol * 256];
        channelrightvol_lookup[i] = &vol_lookup[rightvol * 256];
        channelstep[i] = steptable[pitch];
        return;
    }
}

void I_ShutdownSound(void)
{
    int i;

    if (s_waveout)
    {
        waveOutReset(s_waveout);
        for (i = 0; i < WAVE_BUFFER_COUNT; i++)
        {
            if (s_waveprepared[i])
            {
                waveOutUnprepareHeader(s_waveout, &s_waveheaders[i], sizeof(WAVEHDR));
                s_waveprepared[i] = 0;
            }
        }
        waveOutClose(s_waveout);
        s_waveout = NULL;
    }

    for (i = 1; i < NUMSFX; i++)
    {
        if (!S_sfx[i].link && S_sfx[i].data)
            free((unsigned char *)S_sfx[i].data - 8);
        S_sfx[i].data = 0;
    }

    s_soundready = 0;
}

void I_InitSound(void)
{
    WAVEFORMATEX format;
    MMRESULT result;
    int i;
    int linkindex;

    memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = SAMPLERATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = (WORD)(format.nChannels * (format.wBitsPerSample / 8));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    result = waveOutOpen(&s_waveout, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR)
    {
        fprintf(stderr, "I_InitSound: waveOutOpen failed, sound disabled\n");
        s_waveout = NULL;
        s_soundready = 0;
        return;
    }

    I_SetChannels();
    memset(mixbuffer, 0, sizeof(mixbuffer));
    memset(s_waveheaders, 0, sizeof(s_waveheaders));
    memset(s_wavebuffers, 0, sizeof(s_wavebuffers));
    memset(s_waveprepared, 0, sizeof(s_waveprepared));
    s_nextbuffer = 0;

    for (i = 1; i < NUMSFX; i++)
    {
        if (!S_sfx[i].link)
        {
            S_sfx[i].data = getsfx(S_sfx[i].name, &lengths[i]);
        }
        else
        {
            linkindex = (int)(S_sfx[i].link - S_sfx);
            S_sfx[i].data = S_sfx[linkindex].data;
            lengths[i] = lengths[linkindex];
        }
    }

    s_soundready = 1;
    fprintf(stderr, "I_InitSound: waveOut sound backend ready\n");
}

void I_InitMusic(void)
{
    memset(s_songslots, 0, sizeof(s_songslots));
    s_playing_song = 0;
    s_musicpaused = 0;
    s_music_name[0] = '\0';
    s_music_lumpnum = -1;
    s_music_lumplength = 0;
}

void I_ShutdownMusic(void)
{
    int i;

    close_current_music();
    for (i = 0; i < MAX_REGISTERED_SONGS; i++)
        release_song_slot(i);
}

void I_SetMusicVolume(int volume)
{
    snd_MusicVolume = volume;
    apply_music_volume();
}

void I_SetMusicLump(const char *name, int lumpnum)
{
    copy_string(s_music_name, sizeof(s_music_name), name);
    s_music_lumpnum = lumpnum;
    s_music_lumplength = lumpnum >= 0 ? W_LumpLength(lumpnum) : 0;
}

void I_PauseSong(int handle)
{
    MCIERROR error;

    handle = handle;
    if (!s_playing_song || s_musicpaused)
        return;

    error = mciSendStringA("pause " MUSIC_ALIAS, NULL, 0, NULL);
    if (!error)
        s_musicpaused = 1;
}

void I_ResumeSong(int handle)
{
    MCIERROR error;

    handle = handle;
    if (!s_playing_song || !s_musicpaused)
        return;

    error = mciSendStringA("resume " MUSIC_ALIAS, NULL, 0, NULL);
    if (!error)
        s_musicpaused = 0;
}

int I_RegisterSong(void *data)
{
    unsigned char *loaded_lump;
    const unsigned char *song_data;
    int song_length;
    int slot;
    songtype_t type;

    loaded_lump = NULL;
    song_data = (const unsigned char *)data;
    song_length = s_music_lumplength;

    slot = allocate_song_slot();
    if (slot < 0)
    {
        fprintf(stderr, "I_RegisterSong: no free song slots\n");
        return 0;
    }

    memset(&s_songslots[slot], 0, sizeof(s_songslots[slot]));
    s_songslots[slot].in_use = 1;

    if (find_external_music_file(s_songslots[slot].path, sizeof(s_songslots[slot].path), &type))
    {
        s_songslots[slot].type = type;
        fprintf(stderr, "I_RegisterSong: using external music %s\n", s_songslots[slot].path);
        return slot + 1;
    }

    if (!song_data && s_music_lumpnum >= 0 && song_length > 0)
    {
        loaded_lump = (unsigned char *)malloc((size_t)song_length);
        if (!loaded_lump)
        {
            release_song_slot(slot);
            fprintf(stderr, "I_RegisterSong: failed allocation of %d bytes for music lump %s\n",
                song_length, s_music_name);
            return 0;
        }

        W_ReadLump(s_music_lumpnum, loaded_lump);
        song_data = loaded_lump;
    }

    if (!song_data || song_length <= 0)
    {
        free(loaded_lump);
        release_song_slot(slot);
        fprintf(stderr, "I_RegisterSong: missing music lump data for %s\n", s_music_name);
        return 0;
    }

    if (song_length >= 4 && memcmp(song_data, "MUS\x1a", 4) == 0)
    {
        if (!create_temp_midi_file(song_data, song_length,
            s_songslots[slot].path, sizeof(s_songslots[slot].path)))
        {
            free(loaded_lump);
            release_song_slot(slot);
            fprintf(stderr, "I_RegisterSong: failed to convert MUS %s to MIDI (%s)\n",
                s_music_name, s_music_error);
            return 0;
        }

        s_songslots[slot].temporary = 1;
        s_songslots[slot].type = SONG_TYPE_MIDI;
        fprintf(stderr, "I_RegisterSong: converted %s to temporary MIDI\n", s_music_name);
    }
    else if (song_length >= 4 && memcmp(song_data, "MThd", 4) == 0)
    {
        if (!create_temp_music_file(song_data, song_length, ".mid",
            s_songslots[slot].path, sizeof(s_songslots[slot].path)))
        {
            free(loaded_lump);
            release_song_slot(slot);
            fprintf(stderr, "I_RegisterSong: failed to create temporary MIDI %s (%s)\n",
                s_music_name, s_music_error);
            return 0;
        }

        s_songslots[slot].temporary = 1;
        s_songslots[slot].type = SONG_TYPE_MIDI;
        fprintf(stderr, "I_RegisterSong: loaded embedded MIDI %s\n", s_music_name);
    }
    else if (song_length >= 4 && memcmp(song_data, "OggS", 4) == 0)
    {
        if (!OGG_WriteTempWavFromMemory(song_data, song_length,
            s_songslots[slot].path, sizeof(s_songslots[slot].path)))
        {
            free(loaded_lump);
            release_song_slot(slot);
            fprintf(stderr, "I_RegisterSong: failed to decode OGG %s (%s)\n",
                s_music_name, s_music_error);
            return 0;
        }

        s_songslots[slot].temporary = 1;
        s_songslots[slot].type = SONG_TYPE_DIGITAL;
        fprintf(stderr, "I_RegisterSong: loaded embedded OGG %s\n", s_music_name);
    }
    else
    {
        free(loaded_lump);
        release_song_slot(slot);
        fprintf(stderr, "I_RegisterSong: unsupported embedded music format for %s\n",
            s_music_name);
        return 0;
    }

    free(loaded_lump);
    return slot + 1;
}

void I_PlaySong(int handle, int looping)
{
    MCIERROR error;
    char command[64];
    int slot_index;

    slot_index = handle - 1;
    if (slot_index < 0 || slot_index >= MAX_REGISTERED_SONGS || !s_songslots[slot_index].in_use)
        return;

    if (!open_music_alias(&s_songslots[slot_index]))
        return;

    apply_music_volume();

    if (looping)
    {
        snprintf(command, sizeof(command), "play %s from 0 repeat", MUSIC_ALIAS);
        error = mciSendStringA(command, NULL, 0, NULL);
        if (error)
        {
            snprintf(command, sizeof(command), "play %s from 0", MUSIC_ALIAS);
            error = mciSendStringA(command, NULL, 0, NULL);
        }
    }
    else
    {
        snprintf(command, sizeof(command), "play %s from 0", MUSIC_ALIAS);
        error = mciSendStringA(command, NULL, 0, NULL);
    }

    if (error)
    {
        report_mci_error("I_PlaySong: play failed", error);
        close_current_music();
        return;
    }

    s_playing_song = handle;
    s_musicpaused = 0;
}

void I_StopSong(int handle)
{
    if (handle == s_playing_song)
        close_current_music();
}

void I_UnRegisterSong(int handle)
{
    release_song_slot(handle - 1);
    handle = 0;
}
