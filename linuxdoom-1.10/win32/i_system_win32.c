#define boolean windows_boolean_workaround
#include <windows.h>
#undef boolean

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d_net.h"
#include "g_game.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_video.h"
#include "m_misc.h"

int mb_used = 6;

static LARGE_INTEGER s_qpfreq;
static LARGE_INTEGER s_qpbase;
static int s_time_init;

void I_Tactile(int on, int off, int total)
{
    on = off = total = 0;
}

ticcmd_t emptycmd;
ticcmd_t *I_BaseTiccmd(void)
{
    return &emptycmd;
}

int I_GetHeapSize(void)
{
    return mb_used * 1024 * 1024;
}

byte *I_ZoneBase(int *size)
{
    *size = mb_used * 1024 * 1024;
    return (byte *)malloc(*size);
}

int I_GetTime(void)
{
    LARGE_INTEGER now;
    LONGLONG elapsed;

    if (!s_time_init)
    {
        QueryPerformanceFrequency(&s_qpfreq);
        QueryPerformanceCounter(&s_qpbase);
        s_time_init = 1;
    }

    QueryPerformanceCounter(&now);
    elapsed = now.QuadPart - s_qpbase.QuadPart;

    return (int)((elapsed * TICRATE) / s_qpfreq.QuadPart);
}

void I_Init(void)
{
    I_InitSound();
}

void I_Quit(void)
{
    D_QuitNetGame();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults();
    I_ShutdownGraphics();
    exit(0);
}

void I_WaitVBL(int count)
{
    DWORD ms = (DWORD)((count * 1000) / 70);
    if (ms == 0)
        ms = 1;
    Sleep(ms);
}

void I_BeginRead(void)
{
}

void I_EndRead(void)
{
}

byte *I_AllocLow(int length)
{
    byte *mem = (byte *)malloc(length);
    memset(mem, 0, length);
    return mem;
}

extern boolean demorecording;

void I_Error(char *error, ...)
{
    va_list argptr;

    va_start(argptr, error);
    fprintf(stderr, "Error: ");
    vfprintf(stderr, error, argptr);
    fprintf(stderr, "\n");
    va_end(argptr);

    fflush(stderr);

    if (demorecording)
        G_CheckDemoStatus();

    D_QuitNetGame();
    I_ShutdownGraphics();

    exit(-1);
}
