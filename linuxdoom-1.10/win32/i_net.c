//----------------------------------------------------------
//            DOOM'93 Win32 network stub
//
//  Minimal network implementation for single player
//
//  veeλnti is responsible for this
//----------------------------------------------------------

#include "doomdef.h"
#include "doomstat.h"
#include "d_net.h"
#include "i_system.h"
#include "m_argv.h"
#include <stdlib.h>
#include <stdlib.h>

doomcom_t *doomcom;

void I_InitNetwork(void)
{
    doomcom = (doomcom_t *)calloc(1, sizeof(*doomcom));
    if (!doomcom)
    {
        I_Error("calloc failed for doomcom");
    }

    doomcom->ticdup = 1;
    doomcom->extratics = 0;
    doomcom->id = DOOMCOM_ID;
    doomcom->consoleplayer = 0;

    netgame = false;
    doomcom->numplayers = 1;
    doomcom->numnodes = 1;
    doomcom->deathmatch = 0;
}

void I_NetCmd(void)
{
    if (doomcom->command == CMD_SEND)
    {
        // No-op in single player
    }
    else if (doomcom->command == CMD_GET)
    {
        // No packets in single player
        doomcom->remotenode = -1;
        doomcom->datalength = 0;
    }
}
