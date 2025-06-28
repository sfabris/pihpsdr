#ifndef _WINDOWS_SYS_IOCTL_H_
#define _WINDOWS_SYS_IOCTL_H_

/*
 * Windows compatibility wrapper for sys/ioctl.h
 * This header provides device I/O control operations
 */

#ifdef _WIN32

#include <windows.h>
#include <winsock2.h>
#include <stdarg.h>

/* Common ioctl commands (most are not directly applicable on Windows) */
#ifndef FIONREAD
#define FIONREAD    0x4004667F
#endif

#ifndef FIONBIO
#define FIONBIO     0x8004667E
#endif

#ifndef SIOCGIFCONF
#define SIOCGIFCONF 0x8912
#endif

#ifndef SIOCGIFADDR
#define SIOCGIFADDR 0x8915
#endif

#ifndef SIOCGIFNETMASK
#define SIOCGIFNETMASK 0x891b
#endif

/* Windows equivalent function for socket ioctl operations */
static inline int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    
    /* For socket operations, use ioctlsocket */
    if (request == FIONBIO || request == FIONREAD) {
        u_long *argp = va_arg(args, u_long*);
        va_end(args);
        return ioctlsocket(fd, request, argp);
    }
    
    va_end(args);
    /* For other operations, return not supported */
    WSASetLastError(WSAEOPNOTSUPP);
    return -1;
}

#else
/* Unix/Linux - use system sys/ioctl.h */
#include <sys/ioctl.h>
#endif /* _WIN32 */

#endif /* _WINDOWS_SYS_IOCTL_H_ */
