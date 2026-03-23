#ifndef DOOM_UNISTD_COMPAT_H
#define DOOM_UNISTD_COMPAT_H

#ifdef _WIN32

#include <io.h>
#include <process.h>
#include <stdlib.h>

#ifndef F_OK
#define F_OK 0
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif

#define access _access
#define read _read
#define write _write
#define close _close
#define getpid _getpid

static __inline int usleep(unsigned int usec)
{
    unsigned int ms = usec / 1000U;
    if (ms == 0U)
        ms = 1U;
    _sleep(ms);
    return 0;
}

#else

#include_next <unistd.h>

#endif

#endif
