#ifndef DOOM_ALLOCA_COMPAT_H
#define DOOM_ALLOCA_COMPAT_H

#include <malloc.h>

#ifdef _MSC_VER
#ifndef alloca
#define alloca _alloca
#endif
#endif

#endif
