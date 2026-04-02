// r_dynamic.c - Dynamic array management implementation

#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "r_dynamic.h"
#include "r_resolution.h"
#include "i_system.h"

// Dynamic arrays
short *floorclip_dynamic = NULL;
short *ceilingclip_dynamic = NULL;
fixed_t *yslope_dynamic = NULL;
fixed_t *distscale_dynamic = NULL;
fixed_t *cachedheight_dynamic = NULL;
fixed_t *cacheddistance_dynamic = NULL;
fixed_t *cachedxstep_dynamic = NULL;
fixed_t *cachedystep_dynamic = NULL;
int *spanstart_dynamic = NULL;
int *spanstop_dynamic = NULL;
short *negonearray_dynamic = NULL;
short *screenheightarray_dynamic = NULL;
short *openings_dynamic = NULL;
angle_t *xtoviewangle_dynamic = NULL;

//
// R_InitDynamicArrays
// Allocate all resolution-dependent arrays
//
void R_InitDynamicArrays(void)
{
    int i;
    int maxopenings;

    // Free existing arrays if any
    R_FreeDynamicArrays();

    // Allocate arrays based on current resolution
    floorclip_dynamic = (short *)malloc(SCREENWIDTH * sizeof(short));
    ceilingclip_dynamic = (short *)malloc(SCREENWIDTH * sizeof(short));
    yslope_dynamic = (fixed_t *)malloc(SCREENHEIGHT * sizeof(fixed_t));
    distscale_dynamic = (fixed_t *)malloc(SCREENWIDTH * sizeof(fixed_t));
    cachedheight_dynamic = (fixed_t *)malloc(SCREENHEIGHT * sizeof(fixed_t));
    cacheddistance_dynamic = (fixed_t *)malloc(SCREENHEIGHT * sizeof(fixed_t));
    cachedxstep_dynamic = (fixed_t *)malloc(SCREENHEIGHT * sizeof(fixed_t));
    cachedystep_dynamic = (fixed_t *)malloc(SCREENHEIGHT * sizeof(fixed_t));
    spanstart_dynamic = (int *)malloc(SCREENHEIGHT * sizeof(int));
    spanstop_dynamic = (int *)malloc(SCREENHEIGHT * sizeof(int));
    negonearray_dynamic = (short *)malloc(SCREENWIDTH * sizeof(short));
    screenheightarray_dynamic = (short *)malloc(SCREENWIDTH * sizeof(short));
    xtoviewangle_dynamic = (angle_t *)malloc((SCREENWIDTH + 1) * sizeof(angle_t));

    // Openings array for visplanes
    maxopenings = SCREENWIDTH * 128;
    openings_dynamic = (short *)malloc(maxopenings * sizeof(short));

    // Check allocations
    if (!floorclip_dynamic || !ceilingclip_dynamic || !yslope_dynamic ||
        !distscale_dynamic || !cachedheight_dynamic || !cacheddistance_dynamic ||
        !cachedxstep_dynamic || !cachedystep_dynamic || !spanstart_dynamic ||
        !spanstop_dynamic || !negonearray_dynamic || !screenheightarray_dynamic ||
        !openings_dynamic || !xtoviewangle_dynamic)
    {
        I_Error("R_InitDynamicArrays: Failed to allocate memory for %dx%d resolution",
                SCREENWIDTH, SCREENHEIGHT);
    }

    // Initialize negonearray and screenheightarray
    for (i = 0; i < SCREENWIDTH; i++)
    {
        negonearray_dynamic[i] = -1;
        screenheightarray_dynamic[i] = (short)SCREENHEIGHT;
    }

    // Initialize cached arrays
    memset(cachedheight_dynamic, 0, SCREENHEIGHT * sizeof(fixed_t));
    memset(cacheddistance_dynamic, 0, SCREENHEIGHT * sizeof(fixed_t));
    memset(cachedxstep_dynamic, 0, SCREENHEIGHT * sizeof(fixed_t));
    memset(cachedystep_dynamic, 0, SCREENHEIGHT * sizeof(fixed_t));
}

//
// R_FreeDynamicArrays
// Free all dynamically allocated arrays
//
void R_FreeDynamicArrays(void)
{
    if (floorclip_dynamic)
    {
        free(floorclip_dynamic);
        floorclip_dynamic = NULL;
    }
    if (ceilingclip_dynamic)
    {
        free(ceilingclip_dynamic);
        ceilingclip_dynamic = NULL;
    }
    if (yslope_dynamic)
    {
        free(yslope_dynamic);
        yslope_dynamic = NULL;
    }
    if (distscale_dynamic)
    {
        free(distscale_dynamic);
        distscale_dynamic = NULL;
    }
    if (cachedheight_dynamic)
    {
        free(cachedheight_dynamic);
        cachedheight_dynamic = NULL;
    }
    if (cacheddistance_dynamic)
    {
        free(cacheddistance_dynamic);
        cacheddistance_dynamic = NULL;
    }
    if (cachedxstep_dynamic)
    {
        free(cachedxstep_dynamic);
        cachedxstep_dynamic = NULL;
    }
    if (cachedystep_dynamic)
    {
        free(cachedystep_dynamic);
        cachedystep_dynamic = NULL;
    }
    if (spanstart_dynamic)
    {
        free(spanstart_dynamic);
        spanstart_dynamic = NULL;
    }
    if (spanstop_dynamic)
    {
        free(spanstop_dynamic);
        spanstop_dynamic = NULL;
    }
    if (negonearray_dynamic)
    {
        free(negonearray_dynamic);
        negonearray_dynamic = NULL;
    }
    if (screenheightarray_dynamic)
    {
        free(screenheightarray_dynamic);
        screenheightarray_dynamic = NULL;
    }
    if (openings_dynamic)
    {
        free(openings_dynamic);
        openings_dynamic = NULL;
    }
    if (xtoviewangle_dynamic)
    {
        free(xtoviewangle_dynamic);
        xtoviewangle_dynamic = NULL;
    }
}
