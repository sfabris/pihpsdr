#ifndef _WINDOWS_NETINET_TCP_H_
#define _WINDOWS_NETINET_TCP_H_

/*
 * Windows compatibility wrapper for netinet/tcp.h
 * This header provides TCP protocol definitions
 */

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

/* TCP protocol options */
#ifndef TCP_NODELAY
#define TCP_NODELAY     1
#endif

#ifndef TCP_MAXSEG
#define TCP_MAXSEG      2
#endif

#ifndef TCP_CORK
#define TCP_CORK        3
#endif

#ifndef TCP_KEEPIDLE
#define TCP_KEEPIDLE    4
#endif

#ifndef TCP_KEEPINTVL
#define TCP_KEEPINTVL   5
#endif

#ifndef TCP_KEEPCNT
#define TCP_KEEPCNT     6
#endif

#ifndef TCP_SYNCNT
#define TCP_SYNCNT      7
#endif

#ifndef TCP_LINGER2
#define TCP_LINGER2     8
#endif

#ifndef TCP_DEFER_ACCEPT
#define TCP_DEFER_ACCEPT 9
#endif

#ifndef TCP_WINDOW_CLAMP
#define TCP_WINDOW_CLAMP 10
#endif

#ifndef TCP_INFO
#define TCP_INFO        11
#endif

#ifndef TCP_QUICKACK
#define TCP_QUICKACK    12
#endif

/* SOL_TCP is sometimes needed for setsockopt */
#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif

#else
/* Unix/Linux - use system netinet/tcp.h */
#include <netinet/tcp.h>
#endif /* _WIN32 */

#endif /* _WINDOWS_NETINET_TCP_H_ */
