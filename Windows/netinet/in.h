/* netinet/in.h replacement for Windows */
#ifndef _WINDOWS_NETINET_IN_H
#define _WINDOWS_NETINET_IN_H

#ifdef _WIN32
#include "../windows_compat.h"
#else
#include_next <netinet/in.h>
#endif

#endif
