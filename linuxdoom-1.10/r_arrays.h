// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Compatibility layer for dynamic resolution arrays.
//	Redirects static array names to dynamic pointers.
//	Added for enhanced resolution support.
//
//-----------------------------------------------------------------------------


#ifndef __R_ARRAYS__
#define __R_ARRAYS__

#include "r_dynamic.h"


//
// Redirect static array names to dynamic pointers.
// This allows existing code to work without modification.
//

// From r_plane.c
#define floorclip		floorclip_dynamic
#define ceilingclip		ceilingclip_dynamic
#define yslope			yslope_dynamic
#define distscale		distscale_dynamic
#define cachedheight		cachedheight_dynamic
#define cacheddistance		cacheddistance_dynamic
#define cachedxstep		cachedxstep_dynamic
#define cachedystep		cachedystep_dynamic
#define spanstart		spanstart_dynamic
#define spanstop		spanstop_dynamic
#define openings		openings_dynamic

// From r_things.c
#define negonearray		negonearray_dynamic
#define screenheightarray	screenheightarray_dynamic

// From r_main.c
#define xtoviewangle		xtoviewangle_dynamic


#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
