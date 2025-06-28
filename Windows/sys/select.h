#ifndef _WINDOWS_SYS_SELECT_H_
#define _WINDOWS_SYS_SELECT_H_

/*
 * Windows compatibility wrapper for sys/select.h
 * Windows provides select functionality through winsock2.h
 */

#ifdef _WIN32

/* Windows already provides select functionality through winsock2.h */
#include <winsock2.h>

/* Windows defines these in winsock2.h already */
/* fd_set, FD_ZERO, FD_SET, FD_CLR, FD_ISSET, select() are all available */

/* Additional compatibility definitions if needed */
#ifndef FD_SETSIZE
#define FD_SETSIZE 64
#endif

#else
/* Unix/Linux - use system sys/select.h */
#include <sys/select.h>
#endif /* _WIN32 */

#endif /* _WINDOWS_SYS_SELECT_H_ */
