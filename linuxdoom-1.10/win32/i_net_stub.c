#include <stdlib.h>
#include <string.h>

#include "d_net.h"

extern doomcom_t *doomcom;

void I_InitNetwork(void)
{
    doomcom = (doomcom_t *)malloc(sizeof(*doomcom));
    if (!doomcom)
        return;

    memset(doomcom, 0, sizeof(*doomcom));
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = 1;
    doomcom->numnodes = 1;
    doomcom->consoleplayer = 0;
    doomcom->ticdup = 1;
    doomcom->extratics = 0;
    doomcom->deathmatch = 0;
}

void I_NetCmd(void)
{
    if (!doomcom)
        return;

    if (doomcom->command == CMD_GET)
    {
        doomcom->remotenode = -1;
        doomcom->datalength = 0;
    }
}
