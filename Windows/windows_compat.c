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

#include <windows.h>
#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "windows_compat.h"

/* Windows implementation of clock_gettime */
int clock_gettime(int clk_id, struct timespec *tp) {
    static LARGE_INTEGER frequency;
    static BOOL frequency_initialized = FALSE;
    LARGE_INTEGER count;
    
    if (!frequency_initialized) {
        QueryPerformanceFrequency(&frequency);
        frequency_initialized = TRUE;
    }
    
    switch (clk_id) {
        case CLOCK_REALTIME: {
            FILETIME ft;
            ULARGE_INTEGER ui;
            GetSystemTimeAsFileTime(&ft);
            ui.LowPart = ft.dwLowDateTime;
            ui.HighPart = ft.dwHighDateTime;
            
            /* Convert from 100ns intervals since 1601-01-01 to seconds since 1970-01-01 */
            ui.QuadPart -= 116444736000000000ULL;
            tp->tv_sec = ui.QuadPart / 10000000ULL;
            tp->tv_nsec = (ui.QuadPart % 10000000ULL) * 100;
            return 0;
        }
        
        case CLOCK_MONOTONIC: {
            if (QueryPerformanceCounter(&count)) {
                tp->tv_sec = count.QuadPart / frequency.QuadPart;
                tp->tv_nsec = (long)((count.QuadPart % frequency.QuadPart) * 1000000000LL / frequency.QuadPart);
                return 0;
            }
            return -1;
        }
        
        default:
            return -1;
    }
}

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

#endif /* _WIN32 */
