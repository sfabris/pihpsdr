/* unistd.h replacement for Windows */
#ifndef _WINDOWS_UNISTD_H
#define _WINDOWS_UNISTD_H

#ifdef _WIN32
#include "../windows_compat.h"
#else
#include_next <unistd.h>
#endif

#endif
