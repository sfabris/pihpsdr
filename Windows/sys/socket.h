/* sys/socket.h replacement for Windows */
#ifndef _WINDOWS_SYS_SOCKET_H
#define _WINDOWS_SYS_SOCKET_H

#ifdef _WIN32
#include "../windows_compat.h"
#else
#include_next <sys/socket.h>
#endif

#endif
