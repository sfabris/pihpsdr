/* semaphore.h replacement for Windows */
#ifndef _WINDOWS_SEMAPHORE_H
#define _WINDOWS_SEMAPHORE_H

#ifdef _WIN32
#include "windows_compat.h"
#else
#include_next <semaphore.h>
#endif

#endif
