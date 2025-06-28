#ifndef _WINDOWS_POLL_H_
#define _WINDOWS_POLL_H_

/*
 * Windows compatibility wrapper for poll.h
 * Windows already has poll definitions in winsock2.h, so we just include that
 */

#ifdef _WIN32

/* On Windows, poll functionality is provided by winsock2.h */
#include <winsock2.h>

/* Windows already defines:
 * - POLLIN, POLLOUT, POLLERR, POLLHUP, POLLNVAL constants
 * - struct pollfd 
 * - WSAPoll function (which is equivalent to poll)
 */

/* Define poll as WSAPoll for compatibility */
#define poll WSAPoll

/* Define nfds_t if not already defined */
#ifndef _NFDS_T_DEFINED
#define _NFDS_T_DEFINED
typedef unsigned long nfds_t;
#endif

#else
/* Unix/Linux - use system poll.h */
#include <poll.h>
#endif /* _WIN32 */

#endif /* _WINDOWS_POLL_H_ */
