//----------------------------------------------------------
//            DOOM'93 OGG Temporary File Handler
//
//  OGG file to WAV temporary conversion for sound system
//
//  This is rea;;y just for SIGIL using .ogg music files.
//
//  veeλnti is responsible for this
//----------------------------------------------------------

#ifndef OGG_TEMP_H
#define OGG_TEMP_H

#include <stddef.h>

int OGG_WriteTempWavFromMemory(const unsigned char *ogg_data, int ogg_len,
    char *out_path, size_t out_path_size);

#endif
