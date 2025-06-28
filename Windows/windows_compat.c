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

#ifdef _WIN32

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <lmcons.h>
#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <iphlpapi.h>
#include "windows_compat.h"

/* Include our compatibility headers to get struct definitions */
#include "ifaddrs.h"
#include "termios.h"
#include "pwd.h"

/* clock_gettime is already provided by MinGW, so we don't need our implementation */

/* POSIX semaphore implementation for Windows */
int sem_init(sem_t *sem, int pshared, unsigned int value) {
    (void)pshared; // Ignored on Windows
    
    sem->handle = CreateSemaphore(NULL, value, 0x7FFFFFFF, NULL);
    return (sem->handle != NULL) ? 0 : -1;
}

int sem_destroy(sem_t *sem) {
    if (sem->handle != NULL) {
        CloseHandle(sem->handle);
        sem->handle = NULL;
        return 0;
    }
    return -1;
}

int sem_wait(sem_t *sem) {
    DWORD result = WaitForSingleObject(sem->handle, INFINITE);
    return (result == WAIT_OBJECT_0) ? 0 : -1;
}

int sem_trywait(sem_t *sem) {
    DWORD result = WaitForSingleObject(sem->handle, 0);
    return (result == WAIT_OBJECT_0) ? 0 : -1;
}

int sem_post(sem_t *sem) {
    return ReleaseSemaphore(sem->handle, 1, NULL) ? 0 : -1;
}

int sem_close(sem_t *sem) {
    // For unnamed semaphores, sem_close is the same as sem_destroy
    return sem_destroy(sem);
}

/* POSIX thread implementation for Windows */
typedef struct {
    void *(*start_routine)(void*);
    void *arg;
} thread_data_t;

static DWORD WINAPI thread_start(LPVOID param) {
    thread_data_t *data = (thread_data_t*)param;
    void *result = data->start_routine(data->arg);
    free(data);
    return (DWORD)(uintptr_t)result;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void*), void *arg) {
    (void)attr; // Ignored for simplicity
    
    thread_data_t *data = malloc(sizeof(thread_data_t));
    if (!data) return -1;
    
    data->start_routine = start_routine;
    data->arg = arg;
    
    *thread = CreateThread(NULL, 0, thread_start, data, 0, NULL);
    return (*thread != NULL) ? 0 : -1;
}

int pthread_join(pthread_t thread, void **retval) {
    DWORD result = WaitForSingleObject(thread, INFINITE);
    if (retval) {
        DWORD exit_code;
        GetExitCodeThread(thread, &exit_code);
        *retval = (void*)(uintptr_t)exit_code;
    }
    CloseHandle(thread);
    return (result == WAIT_OBJECT_0) ? 0 : -1;
}

int pthread_detach(pthread_t thread) {
    CloseHandle(thread);
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    (void)attr; // Ignored for simplicity
    InitializeCriticalSection(mutex);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    DeleteCriticalSection(mutex);
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    EnterCriticalSection(mutex);
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (TryEnterCriticalSection(mutex)) {
        return 0;  /* Success */
    } else {
        return EBUSY;  /* Would block */
    }
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    LeaveCriticalSection(mutex);
    return 0;
}

int pthread_attr_init(pthread_attr_t *attr) {
    (void)attr;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
    (void)attr;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate) {
    (void)attr;
    (void)detachstate;
    return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
    (void)attr;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
    (void)attr;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
    (void)attr;
    (void)type;
    return 0;
}

/* Additional POSIX compatibility function implementations */

int fcntl(int fd, int cmd, ...) {
    va_list args;
    va_start(args, cmd);
    
    switch (cmd) {
        case F_GETFL: {
            // On Windows, we can't really get socket flags like O_NONBLOCK
            // Return 0 as a reasonable default
            va_end(args);
            return 0;
        }
        case F_SETFL: {
            int flags = va_arg(args, int);
            va_end(args);
            
            if (flags & O_NONBLOCK) {
                // Set socket to non-blocking mode
                u_long mode = 1;
                return ioctlsocket(fd, FIONBIO, &mode);
            }
            return 0;
        }
        default:
            va_end(args);
            errno = EINVAL;
            return -1;
    }
}

void bcopy(const void *src, void *dest, size_t n) {
    memmove(dest, src, n);
}

char *realpath(const char *path, char *resolved_path) {
    DWORD length;
    static char static_buffer[PATH_MAX];
    char *buffer = resolved_path ? resolved_path : static_buffer;
    
    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    
    length = GetFullPathNameA(path, PATH_MAX, buffer, NULL);
    if (length == 0) {
        errno = ENOENT;
        return NULL;
    }
    
    if (length >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    
    // Check if file exists
    DWORD attributes = GetFileAttributesA(buffer);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        errno = ENOENT;
        return NULL;
    }
    
    return buffer;
}

/* POSIX uname() implementation for Windows */
int uname(struct utsname *buf) {
    OSVERSIONINFOW osvi;
    SYSTEM_INFO si;
    DWORD size;
    char hostname[_UTSNAME_LENGTH];

    if (!buf) {
        errno = EFAULT;
        return -1;
    }

    memset(buf, 0, sizeof(struct utsname));

    /* Operating system name */
    strcpy(buf->sysname, "Windows");

    /* Get hostname */
    size = sizeof(hostname);
    if (GetComputerNameA(hostname, &size)) {
        strncpy(buf->nodename, hostname, _UTSNAME_LENGTH - 1);
        buf->nodename[_UTSNAME_LENGTH - 1] = '\0';
    } else {
        strcpy(buf->nodename, "localhost");
    }

    /* Get Windows version information */
    memset(&osvi, 0, sizeof(OSVERSIONINFOW));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);

    /* For Windows 10 and later, GetVersionEx may not return accurate info */
    /* But we'll use what's available */
    if (GetVersionExW(&osvi)) {
        snprintf(buf->release, _UTSNAME_LENGTH - 1, "%lu.%lu", 
                 osvi.dwMajorVersion, osvi.dwMinorVersion);
        snprintf(buf->version, _UTSNAME_LENGTH - 1, "Build %lu", 
                 osvi.dwBuildNumber);
    } else {
        strcpy(buf->release, "unknown");
        strcpy(buf->version, "unknown");
    }

    /* Get processor architecture */
    GetSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            strcpy(buf->machine, "x86_64");
            break;
        case PROCESSOR_ARCHITECTURE_INTEL:
            strcpy(buf->machine, "i386");
            break;
        case PROCESSOR_ARCHITECTURE_ARM:
            strcpy(buf->machine, "arm");
            break;
        case PROCESSOR_ARCHITECTURE_ARM64:
            strcpy(buf->machine, "aarch64");
            break;
        default:
            strcpy(buf->machine, "unknown");
            break;
    }

    return 0;
}

/* Network interface enumeration functions */
int getifaddrs(struct ifaddrs **ifap) {
    PIP_ADAPTER_ADDRESSES pAddresses = NULL;
    ULONG outBufLen = 0;
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
    DWORD dwRetVal = 0;
    struct ifaddrs *ifa_head = NULL, *ifa_tail = NULL;
    PIP_ADAPTER_ADDRESSES pCurrAddresses = NULL;
    PIP_ADAPTER_UNICAST_ADDRESS pUnicast = NULL;

    if (!ifap) {
        return -1;
    }

    *ifap = NULL;

    /* Get the size needed for adapter addresses */
    dwRetVal = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, pAddresses, &outBufLen);
    if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
        pAddresses = (IP_ADAPTER_ADDRESSES *) malloc(outBufLen);
        if (pAddresses == NULL) {
            return -1;
        }
    }

    /* Make the actual call */
    dwRetVal = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, pAddresses, &outBufLen);

    if (dwRetVal == NO_ERROR) {
        pCurrAddresses = pAddresses;
        while (pCurrAddresses) {
            pUnicast = pCurrAddresses->FirstUnicastAddress;
            while (pUnicast != NULL) {
                struct ifaddrs *ifa = (struct ifaddrs *)malloc(sizeof(struct ifaddrs));
                if (!ifa) {
                    freeifaddrs(ifa_head);
                    free(pAddresses);
                    return -1;
                }

                memset(ifa, 0, sizeof(struct ifaddrs));

                /* Convert adapter name from wide char to char */
                int name_len = WideCharToMultiByte(CP_UTF8, 0, pCurrAddresses->FriendlyName, -1, NULL, 0, NULL, NULL);
                ifa->ifa_name = (char *)malloc(name_len);
                if (ifa->ifa_name) {
                    WideCharToMultiByte(CP_UTF8, 0, pCurrAddresses->FriendlyName, -1, ifa->ifa_name, name_len, NULL, NULL);
                }

                /* Set interface flags */
                ifa->ifa_flags = 0;
                if (pCurrAddresses->OperStatus == IfOperStatusUp) {
                    ifa->ifa_flags |= IFF_UP | IFF_RUNNING;
                }
                if (pCurrAddresses->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                    ifa->ifa_flags |= IFF_LOOPBACK;
                }
                if (pCurrAddresses->IfType == IF_TYPE_ETHERNET_CSMACD ||
                    pCurrAddresses->IfType == IF_TYPE_IEEE80211) {
                    ifa->ifa_flags |= IFF_BROADCAST | IFF_MULTICAST;
                }

                /* Copy the address */
                if (pUnicast->Address.lpSockaddr) {
                    int addr_len = pUnicast->Address.iSockaddrLength;
                    ifa->ifa_addr = (struct sockaddr *)malloc(addr_len);
                    if (ifa->ifa_addr) {
                        memcpy(ifa->ifa_addr, pUnicast->Address.lpSockaddr, addr_len);
                    }
                }

                /* Create netmask based on prefix length */
                if (pUnicast->Address.lpSockaddr && pUnicast->Address.lpSockaddr->sa_family == AF_INET) {
                    struct sockaddr_in *netmask = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
                    if (netmask) {
                        memset(netmask, 0, sizeof(struct sockaddr_in));
                        netmask->sin_family = AF_INET;
                        
                        /* Convert prefix length to netmask */
                        int prefix_len = pUnicast->OnLinkPrefixLength;
                        if (prefix_len > 0 && prefix_len <= 32) {
                            uint32_t mask = 0xffffffff << (32 - prefix_len);
                            netmask->sin_addr.s_addr = htonl(mask);
                        } else {
                            /* Default netmask for Class C */
                            netmask->sin_addr.s_addr = htonl(0xffffff00);
                        }
                        
                        ifa->ifa_netmask = (struct sockaddr *)netmask;
                    }
                }

                /* Add to list */
                if (!ifa_head) {
                    ifa_head = ifa_tail = ifa;
                } else {
                    ifa_tail->ifa_next = ifa;
                    ifa_tail = ifa;
                }

                pUnicast = pUnicast->Next;
            }
            pCurrAddresses = pCurrAddresses->Next;
        }
    }

    if (pAddresses) {
        free(pAddresses);
    }

    *ifap = ifa_head;
    return 0;
}

void freeifaddrs(struct ifaddrs *ifa) {
    struct ifaddrs *next;
    
    while (ifa) {
        next = ifa->ifa_next;
        
        if (ifa->ifa_name) {
            free(ifa->ifa_name);
        }
        if (ifa->ifa_addr) {
            free(ifa->ifa_addr);
        }
        if (ifa->ifa_netmask) {
            free(ifa->ifa_netmask);
        }
        if (ifa->ifa_broadaddr) {
            free(ifa->ifa_broadaddr);
        }
        
        free(ifa);
        ifa = next;
    }
}

/* inet_aton - convert Internet host address from numbers-and-dots notation in string to binary form */
int inet_aton(const char *cp, struct in_addr *inp) {
    if (cp == NULL || inp == NULL) {
        return 0;
    }
    
    unsigned long addr = inet_addr(cp);
    if (addr == INADDR_NONE) {
        return 0;
    }
    
    inp->s_addr = addr;
    return 1;
}

/* String functions - index and rindex are POSIX functions similar to strchr/strrchr */
char *index(const char *s, int c) {
    return strchr(s, c);
}

char *rindex(const char *s, int c) {
    return strrchr(s, c);
}

/* Additional termios function implementations for Windows */

int tcgetattr(int fd, struct termios *termios_p) {
    /* Windows doesn't have direct equivalent - provide basic stub */
    if (!termios_p) {
        errno = EFAULT;
        return -1;
    }
    
    /* Initialize with default values */
    memset(termios_p, 0, sizeof(struct termios));
    termios_p->c_cflag = CS8 | CREAD | CLOCAL;
    termios_p->c_iflag = IGNPAR;
    termios_p->c_oflag = 0;
    termios_p->c_lflag = 0;
    termios_p->c_cc[VMIN] = 1;
    termios_p->c_cc[VTIME] = 0;
    
    return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
    /* Windows doesn't have direct equivalent - provide basic stub */
    (void)fd;
    (void)optional_actions;
    (void)termios_p;
    return 0;
}

int tcflush(int fd, int queue_selector) {
    /* Windows doesn't have direct equivalent - provide basic stub */
    (void)fd;
    (void)queue_selector;
    return 0;
}

int tcflow(int fd, int action) {
    /* Windows doesn't have direct equivalent - provide basic stub */
    (void)fd;
    (void)action;
    return 0;
}

speed_t cfgetispeed(const struct termios *termios_p) {
    if (!termios_p) return B0;
    return termios_p->c_ispeed;
}

speed_t cfgetospeed(const struct termios *termios_p) {
    if (!termios_p) return B0;
    return termios_p->c_ospeed;
}

int cfsetispeed(struct termios *termios_p, speed_t speed) {
    if (!termios_p) {
        errno = EFAULT;
        return -1;
    }
    termios_p->c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *termios_p, speed_t speed) {
    if (!termios_p) {
        errno = EFAULT;
        return -1;
    }
    termios_p->c_ospeed = speed;
    return 0;
}

int cfsetspeed(struct termios *termios_p, speed_t speed) {
    if (!termios_p) {
        errno = EFAULT;
        return -1;
    }
    termios_p->c_ispeed = speed;
    termios_p->c_ospeed = speed;
    return 0;
}

/* User database access functions */
static struct passwd pwd_entry = {0};
static char username_buffer[UNLEN + 1];
static char homedir_buffer[MAX_PATH];

struct passwd *getpwuid(uid_t uid) {
    (void)uid; /* Unused on Windows */
    
    DWORD username_len = UNLEN + 1;
    if (!GetUserNameA(username_buffer, &username_len)) {
        return NULL;
    }
    
    /* Get home directory */
    DWORD homedir_len = MAX_PATH;
    if (GetEnvironmentVariableA("USERPROFILE", homedir_buffer, homedir_len) == 0) {
        strcpy(homedir_buffer, "C:\\Users\\Default");
    }
    
    /* Fill passwd structure */
    pwd_entry.pw_name = username_buffer;
    pwd_entry.pw_passwd = "*";  /* No password access */
    pwd_entry.pw_uid = 1000;    /* Fake UID */
    pwd_entry.pw_gid = 1000;    /* Fake GID */
    pwd_entry.pw_gecos = username_buffer;
    pwd_entry.pw_dir = homedir_buffer;
    pwd_entry.pw_shell = "cmd.exe";
    
    return &pwd_entry;
}

uid_t getuid(void) {
    return 1000;  /* Fake UID for Windows */
}

gid_t getgid(void) {
    return 1000;  /* Fake GID for Windows */
}
#endif /* _WIN32 */
