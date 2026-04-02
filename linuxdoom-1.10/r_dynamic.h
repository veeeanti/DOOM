// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Dynamic array management for resolution-independent rendering.
//	Added for enhanced resolution support.
//
//-----------------------------------------------------------------------------


#ifndef __R_DYNAMIC__
#define __R_DYNAMIC__

#include "doomtype.h"
#include "m_fixed.h"
#include "tables.h"


//
// Dynamic arrays that scale with resolution
//
extern short*		floorclip_dynamic;
extern short*		ceilingclip_dynamic;
extern fixed_t*		yslope_dynamic;
extern fixed_t*		distscale_dynamic;
extern fixed_t*		cachedheight_dynamic;
extern fixed_t*		cacheddistance_dynamic;
extern fixed_t*		cachedxstep_dynamic;
extern fixed_t*		cachedystep_dynamic;
extern int*		spanstart_dynamic;
extern int*		spanstop_dynamic;
extern short*		negonearray_dynamic;
extern short*		screenheightarray_dynamic;
extern short*		openings_dynamic;
extern angle_t*		xtoviewangle_dynamic;


//
// Initialize all dynamic arrays based on current SCREENWIDTH/SCREENHEIGHT.
//
void R_InitDynamicArrays (void);

//
// Free all dynamic arrays.
//
void R_FreeDynamicArrays (void);


#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
