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

#endif /* _WIN32 */
