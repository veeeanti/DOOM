// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Dynamic resolution support for runtime configuration.
//	Added for enhanced resolution support.
//
//-----------------------------------------------------------------------------


#ifndef __R_RESOLUTION__
#define __R_RESOLUTION__


// Maximum supported resolution (for array bounds)
#define MAX_SCREENWIDTH		1360
#define MAX_SCREENHEIGHT	768

// Default resolution
#define DEFAULT_WIDTH		960
#define DEFAULT_HEIGHT		600


// Runtime resolution variables
extern int	SCREENWIDTH;
extern int	SCREENHEIGHT;


// Initialize resolution from command line or defaults.
void R_InitResolution (void);


#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
