#ifndef _WINDOWS_POLL_H_
#define _WINDOWS_POLL_H_

/*
 * Windows compatibility wrapper for poll.h
 * Windows winsock2.h already defines poll structures and macros,
 * so we just include that and provide compatibility.
 */

#ifdef _WIN32
#include <winsock2.h>
/* Windows already defines POLLIN, POLLOUT, POLLERR, etc. in winsock2.h */
/* Windows already defines struct pollfd in winsock2.h */
/* poll() function is available in modern Windows versions */

#else
/* Unix/Linux - include the real poll.h */
#include_next <poll.h>
#endif

#endif /* _WINDOWS_POLL_H_ */
            Sleep(timeout);
        }
        return 0;
    }

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

    /* Setup fd_sets */
    for (i = 0; i < nfds; i++) {
        if (fds[i].fd < 0) continue;
        
        if (fds[i].events & (POLLIN | POLLPRI)) {
            FD_SET(fds[i].fd, &readfds);
        }
        if (fds[i].events & POLLOUT) {
            FD_SET(fds[i].fd, &writefds);
#endif /* _WINDOWS_POLL_H_ */
