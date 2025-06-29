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

/* Windows-specific includes and compatibility layer */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <direct.h>

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

#else
/* Unix/Linux includes */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#define PATH_SEPARATOR "/"

static inline int init_winsock(void) { return 0; }
static inline void cleanup_winsock(void) { }

#endif /* _WIN32 */

#endif /* _WINDOWS_COMPAT_H_ */
