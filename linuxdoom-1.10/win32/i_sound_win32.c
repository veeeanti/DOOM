
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
#include "w_wad.h"
#include "z_zone.h"

#define SAMPLECOUNT 512
#define NUM_CHANNELS 8
#define SAMPLERATE 11025
#define BUFFER_SAMPLES (SAMPLECOUNT * 2)
#define MIXBUFFER_BYTES (SAMPLECOUNT * 4)
#define WAVE_BUFFER_COUNT 4

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

#ifdef SNDSERV
FILE *sndserver = NULL;
char *sndserver_filename = "sndserver";
#endif

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
}

void I_ShutdownMusic(void)
{
}

void I_SetMusicVolume(int volume)
{
    snd_MusicVolume = volume;
}

void I_PauseSong(int handle)
{
    handle = 0;
}

void I_ResumeSong(int handle)
{
    handle = 0;
}

int I_RegisterSong(void *data)
{
    data = NULL;
    return 1;
}

void I_PlaySong(int handle, int looping)
{
    handle = looping = 0;
}

void I_StopSong(int handle)
{
    handle = 0;
}

void I_UnRegisterSong(int handle)
{
    handle = 0;
}
