/* pthread.h replacement for Windows */
#ifndef _WINDOWS_PTHREAD_H
#define _WINDOWS_PTHREAD_H

#ifdef _WIN32
#include "windows_compat.h"
#else
#include_next <pthread.h>
#endif

#endif
