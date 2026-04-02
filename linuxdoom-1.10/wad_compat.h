// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Enhanced WAD compatibility for modern PWADs.
//	Fixes crashes with larger/newer WAD files.
//	Added for enhanced WAD support.
//
//-----------------------------------------------------------------------------


#ifndef __WAD_COMPAT__
#define __WAD_COMPAT__


// Increased limits for modern WADs
#define MAX_LUMPS_COMPAT	32768	// Up from ~4000 in original
#define MAX_VISPLANES		2048	// Up from 128
#define MAX_DRAWSEGS		2048	// Up from 256
#define MAX_VISSPRITES		1024	// Up from 128
#define MAX_OPENINGS_MULT	256	// Multiplier for openings array


// Zone memory size (in MB) - adjustable via command line
extern int	zone_memory_mb;


// Initialize enhanced WAD compatibility.
void WAD_InitCompatibility (void);

// Check if we need enhanced limits.
int WAD_NeedsEnhancedLimits (void);


#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
