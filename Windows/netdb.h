/* netdb.h replacement for Windows */
#ifndef _WINDOWS_NETDB_H
#define _WINDOWS_NETDB_H

#ifdef _WIN32
#include "windows_compat.h"
#else
#include_next <netdb.h>
#endif

#endif
