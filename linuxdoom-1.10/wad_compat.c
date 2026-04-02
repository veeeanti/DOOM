// wad_compat.c - Enhanced WAD compatibility implementation

#include <stdio.h>
#include <stdlib.h>
#include "doomdef.h"
#include "wad_compat.h"
#include "m_argv.h"
#include "w_wad.h"

// Default zone memory: 32MB (up from original 8MB)
int zone_memory_mb = 32;

//
// WAD_InitCompatibility
// Initialize enhanced limits and memory for modern WADs
//
void WAD_InitCompatibility(void)
{
    int p;

    // Check for -zonememory parameter
    p = M_CheckParm("-zonememory");
    if (p && p < myargc - 1)
    {
        zone_memory_mb = atoi(myargv[p + 1]);

        // Clamp to reasonable values
        if (zone_memory_mb < 8)
            zone_memory_mb = 8;
        if (zone_memory_mb > 256)
            zone_memory_mb = 256;
    }

    // Check for -mb parameter (alternate syntax)
    p = M_CheckParm("-mb");
    if (p && p < myargc - 1)
    {
        zone_memory_mb = atoi(myargv[p + 1]);

        if (zone_memory_mb < 8)
            zone_memory_mb = 8;
        if (zone_memory_mb > 256)
            zone_memory_mb = 256;
    }

    printf("Zone memory: %d MB\n", zone_memory_mb);
}

//
// WAD_NeedsEnhancedLimits
// Check if loaded WADs require enhanced limits
//
int WAD_NeedsEnhancedLimits(void)
{
    // If we have more than 4000 lumps, we definitely need enhanced limits
    if (numlumps > 4000)
    {
        printf("Large WAD detected (%d lumps), using enhanced limits\n", numlumps);
        return 1;
    }

    // Check for known large PWADs by checking for specific markers
    if (W_CheckNumForName("MAPINFO") >= 0 ||
        W_CheckNumForName("ZMAPINFO") >= 0 ||
        W_CheckNumForName("DECORATE") >= 0)
    {
        printf("Advanced WAD features detected, using enhanced limits\n");
        return 1;
    }

    return 0;
}
