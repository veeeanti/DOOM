#include <stdio.h>

#include "i_sound.h"

static int s_next_handle = 1;

#ifdef SNDSERV
FILE *sndserver = NULL;
char *sndserver_filename = "sndserver";
#endif

void I_InitSound(void)
{
    fprintf(stderr, "I_InitSound: using stub backend on Windows\n");
}

void I_UpdateSound(void)
{
}

void I_SubmitSound(void)
{
}

void I_ShutdownSound(void)
{
}

void I_SetChannels(void)
{
}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
    return sfxinfo->link ? sfxinfo->link->lumpnum : sfxinfo->lumpnum;
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    id = vol = sep = pitch = priority = 0;
    return s_next_handle++;
}

void I_StopSound(int handle)
{
    handle = 0;
}

int I_SoundIsPlaying(int handle)
{
    handle = 0;
    return 0;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    handle = vol = sep = pitch = 0;
}

void I_InitMusic(void)
{
}

void I_ShutdownMusic(void)
{
}

void I_SetMusicVolume(int volume)
{
    volume = 0;
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
