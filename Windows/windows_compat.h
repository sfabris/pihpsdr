/* Copyright (C)
* 2025 - Windows/MinGW port for piHPSDR
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

#ifndef _WINDOWS_COMPAT_H_
#define _WINDOWS_COMPAT_H_

#ifdef _WIN32

/* Prevent Windows headers from defining problematic symbols */
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

/* Windows-specific includes and compatibility layer */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <direct.h>

/* Windows typedef conflicts - undefine after Windows headers */
#ifdef SNB
#undef SNB
#endif

/* Socket compatibility */
#define close(s) closesocket(s)
#define MSG_DONTWAIT 0
#define SHUT_RDWR SD_BOTH

/* POSIX compatibility */
#define sleep(x) Sleep((x)*1000)
#define usleep(x) Sleep((x)/1000)

/* Directory functions */
#define mkdir(path, mode) _mkdir(path)

/* Socket error handling */
#ifndef EWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#endif
#ifndef EAGAIN
#define EAGAIN WSAEWOULDBLOCK
#endif
#ifndef EINPROGRESS
#define EINPROGRESS WSAEINPROGRESS
#endif

/* Network compatibility */
typedef int socklen_t;

/* Initialize Windows Sockets */
static inline int init_winsock(void) {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData);
}

static inline void cleanup_winsock(void) {
    WSACleanup();
}

/* Clock functions compatibility */
#include <time.h>
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

int clock_gettime(int clk_id, struct timespec *tp);

/* Path separator */
#define PATH_SEPARATOR "\\"

/* POSIX semaphore compatibility for Windows */
#ifndef SEM_FAILED
#define SEM_FAILED ((sem_t*)0)
#endif

typedef struct {
    HANDLE handle;
} sem_t;

int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_destroy(sem_t *sem);
int sem_wait(sem_t *sem);
int sem_trywait(sem_t *sem);
int sem_post(sem_t *sem);
int sem_close(sem_t *sem);

/* POSIX thread compatibility */
typedef HANDLE pthread_t;
typedef CRITICAL_SECTION pthread_mutex_t;
typedef struct {
    int dummy;
} pthread_attr_t;

typedef struct {
    int dummy;
} pthread_mutexattr_t;

#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_MUTEX_RECURSIVE 2

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void*), void *arg);
int pthread_join(pthread_t thread, void **retval);
int pthread_detach(pthread_t thread);

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);

int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);

/* File control compatibility */
#include <fcntl.h>
#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x800
#endif

/* Signal handling compatibility */
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIG_IGN
#define SIG_IGN ((void (*)(int))1)
#endif
/* Note: signal() function is already defined by Windows headers */

/* POSIX function declarations for Windows */
int fcntl(int fd, int cmd, ...);
void bcopy(const void *src, void *dest, size_t n);

/* Socket constants not available on Windows */
#ifndef SO_REUSEPORT
#define SO_REUSEPORT SO_REUSEADDR  /* Windows doesn't have SO_REUSEPORT, use SO_REUSEADDR */
#endif

#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP  /* Windows uses IPPROTO_TCP instead of SOL_TCP */
#endif

/* Socket timeout options - Windows uses different constants */
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 0x1006
#endif
#ifndef SO_SNDTIMEO  
#define SO_SNDTIMEO 0x1005
#endif

/* Socket option compatibility - Windows uses char* for option values */
#define GETSOCKOPT_TYPE char
#define SETSOCKOPT_TYPE const char

/* Socket option wrapper macros for Windows */
#define SETSOCKOPT(sockfd, level, optname, optval, optlen) \
    setsockopt(sockfd, level, optname, (SETSOCKOPT_TYPE*)(optval), optlen)
#define GETSOCKOPT(sockfd, level, optname, optval, optlen) \
    getsockopt(sockfd, level, optname, (GETSOCKOPT_TYPE*)(optval), optlen)

#else
/* Unix/Linux includes */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#define PATH_SEPARATOR "/"

/* Socket option wrapper macros for Unix/Linux (pass-through) */
#define SETSOCKOPT(sockfd, level, optname, optval, optlen) \
    setsockopt(sockfd, level, optname, optval, optlen)
#define GETSOCKOPT(sockfd, level, optname, optval, optlen) \
    getsockopt(sockfd, level, optname, optval, optlen)

static inline int init_winsock(void) { return 0; }
static inline void cleanup_winsock(void) { }

#endif /* _WIN32 */

/* POSIX uname functionality */
#include "sys/utsname.h"

#endif /* _WINDOWS_COMPAT_H_ */
