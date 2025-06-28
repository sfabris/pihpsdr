#ifndef _WINDOWS_POLL_H_
#define _WINDOWS_POLL_H_

/*
 * Windows compatibility wrapper for poll.h
 * This provides basic poll functionality using Windows select()
 */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>

/* Poll event flags */
#define POLLIN     0x001   /* Data ready for reading */
#define POLLPRI    0x002   /* Priority data ready for reading */  
#define POLLOUT    0x004   /* Ready for writing */
#define POLLERR    0x008   /* Error condition */
#define POLLHUP    0x010   /* Hung up */
#define POLLNVAL   0x020   /* Invalid request */

/* Poll file descriptor structure */
struct pollfd {
    int   fd;        /* file descriptor */
    short events;    /* requested events */
    short revents;   /* returned events */
};

typedef unsigned long nfds_t;

/* Basic poll implementation using select() */
static inline int poll(struct pollfd fds[], nfds_t nfds, int timeout) {
    fd_set readfds, writefds, exceptfds;
    struct timeval tv;
    int max_fd = -1;
    int ret;
    nfds_t i;

    if (nfds == 0) {
        if (timeout > 0) {
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
        }
        FD_SET(fds[i].fd, &exceptfds);
        
        if (fds[i].fd > max_fd) {
            max_fd = fds[i].fd;
        }
        fds[i].revents = 0;
    }

    /* Setup timeout */
    if (timeout >= 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
    }

    /* Call select */
    ret = select(max_fd + 1, &readfds, &writefds, &exceptfds, 
                 timeout >= 0 ? &tv : NULL);

    if (ret <= 0) {
        return ret;
    }

    /* Check results */
    ret = 0;
    for (i = 0; i < nfds; i++) {
        if (fds[i].fd < 0) continue;

        if (FD_ISSET(fds[i].fd, &readfds)) {
            fds[i].revents |= POLLIN;
        }
        if (FD_ISSET(fds[i].fd, &writefds)) {
            fds[i].revents |= POLLOUT;
        }
        if (FD_ISSET(fds[i].fd, &exceptfds)) {
            fds[i].revents |= POLLERR;
        }
        
        if (fds[i].revents) {
            ret++;
        }
    }

    return ret;
}

#else
/* Unix/Linux - use system poll.h */
#include <poll.h>
#endif /* _WIN32 */

#endif /* _WINDOWS_POLL_H_ */
