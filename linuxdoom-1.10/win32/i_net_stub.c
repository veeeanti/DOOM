#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d_net.h"
#include "doomstat.h"
#include "m_argv.h"
#include "win32/steam_transport.h"

/* GNS is an optional transport. Provide no-op stubs when not compiled in. */
#ifndef DOOM_ENABLE_GNS
static int GNS_InitTransport(doomcom_t *d, int argc, char **argv) { (void)d; (void)argc; (void)argv; return 0; }
static void GNS_NetCmd(doomcom_t *d) { (void)d; }
static const char *GNS_LastError(void) { return "GNS not compiled in"; }
#endif

extern doomcom_t *doomcom;

static int net_transport_active;
static int net_transport_is_steam;

void I_InitNetwork(void)
{
    int i;
    int p;

    doomcom = (doomcom_t *)malloc(sizeof(*doomcom));
    if (!doomcom)
        return;

    memset(doomcom, 0, sizeof(*doomcom));

    i = M_CheckParm("-dup");
    if (i && i < myargc - 1)
    {
        doomcom->ticdup = myargv[i + 1][0] - '0';
        if (doomcom->ticdup < 1)
            doomcom->ticdup = 1;
        if (doomcom->ticdup > 9)
            doomcom->ticdup = 9;
    }
    else
    {
        doomcom->ticdup = 1;
    }

    doomcom->extratics = M_CheckParm("-extratic") ? 1 : 0;
    doomcom->id = DOOMCOM_ID;
    doomcom->consoleplayer = 0;

    p = M_CheckParm("-net");
    if (!p)
    {
        netgame = false;
        doomcom->numplayers = 1;
        doomcom->numnodes = 1;
        doomcom->deathmatch = 0;
        return;
    }

    if (p < myargc - 1)
        doomcom->consoleplayer = myargv[p + 1][0] - '1';

    doomcom->deathmatch = deathmatch;

    if (STEAM_InitTransport(doomcom, myargc, myargv))
    {
        net_transport_active = 1;
        net_transport_is_steam = 1;
        netgame = true;
        printf("I_InitNetwork: Steam transport active\n");
        return;
    }

    /* Keep GNS available as an explicit opt-in fallback while Steam is primary. */
    if (M_CheckParm("-gns") && GNS_InitTransport(doomcom, myargc, myargv))
    {
        net_transport_active = 1;
        net_transport_is_steam = 0;
        netgame = true;
        printf("I_InitNetwork: GameNetworkingSockets transport active\n");
        return;
    }

    net_transport_active = 0;
    net_transport_is_steam = 0;
    netgame = false;
    doomcom->numplayers = 1;
    doomcom->numnodes = 1;
    printf("I_InitNetwork: %s\n", STEAM_LastError());
    if (M_CheckParm("-gns"))
        printf("I_InitNetwork: %s\n", GNS_LastError());
    printf("I_InitNetwork: falling back to single-player mode\n");
}

void I_NetCmd(void)
{
    if (!doomcom)
        return;

    if (net_transport_active)
    {
        if (net_transport_is_steam)
            STEAM_NetCmd(doomcom);
        else
            GNS_NetCmd(doomcom);
        return;
    }

    if (doomcom->command == CMD_GET)
    {
        doomcom->remotenode = -1;
        doomcom->datalength = 0;
    }
}
