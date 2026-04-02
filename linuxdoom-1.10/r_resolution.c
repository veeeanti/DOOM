// r_resolution.c - Dynamic resolution support implementation

#include <stdio.h>
#include <stdlib.h>
#include "doomdef.h"
#include "r_resolution.h"
#include "m_argv.h"

// Runtime resolution variables
int SCREENWIDTH = DEFAULT_WIDTH;
int SCREENHEIGHT = DEFAULT_HEIGHT;

//
// R_InitResolution
// Parse command line for resolution parameters
// Supports: -width <n> -height <n> or -res <width>x<height>
//
void R_InitResolution(void)
{
    int p;
    int width = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;

    // Check for -width parameter
    p = M_CheckParm("-width");
    if (p && p < myargc - 1)
    {
        width = atoi(myargv[p + 1]);
    }

    // Check for -height parameter
    p = M_CheckParm("-height");
    if (p && p < myargc - 1)
    {
        height = atoi(myargv[p + 1]);
    }

    // Check for -res parameter (format: -res 1280x800)
    p = M_CheckParm("-res");
    if (p && p < myargc - 1)
    {
        if (sscanf(myargv[p + 1], "%dx%d", &width, &height) == 2)
        {
            // Successfully parsed resolution
        }
    }

    // Validate and clamp resolution
    if (width < 320)
        width = 320;
    if (width > MAX_SCREENWIDTH)
        width = MAX_SCREENWIDTH;
    if (height < 200)
        height = 200;
    if (height > MAX_SCREENHEIGHT)
        height = MAX_SCREENHEIGHT;

    // Ensure width is multiple of 4 for better performance
    width = (width + 3) & ~3;

    SCREENWIDTH = width;
    SCREENHEIGHT = height;

    printf("Resolution: %dx%d\n", SCREENWIDTH, SCREENHEIGHT);
}
