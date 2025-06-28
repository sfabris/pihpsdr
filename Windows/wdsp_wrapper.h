#ifndef _WDSP_WRAPPER_H_
#define _WDSP_WRAPPER_H_

/*
 * Wrapper for wdsp.h to handle Windows compatibility issues
 */

#ifdef _WIN32

/* Include Windows headers first to establish proper types */
#include <winsock2.h>
#include <windows.h>

/* Save original Windows definitions */
#ifdef DWORD
#define WINDOWS_DWORD DWORD
#undef DWORD
#endif

#ifdef LPCRITICAL_SECTION
#define WINDOWS_LPCRITICAL_SECTION LPCRITICAL_SECTION
#undef LPCRITICAL_SECTION
#endif

/* Include wdsp.h */
#include "../wdsp/wdsp.h"

/* Restore Windows definitions */
#ifdef WINDOWS_DWORD
#undef DWORD
#define DWORD WINDOWS_DWORD
#undef WINDOWS_DWORD
#endif

#ifdef WINDOWS_LPCRITICAL_SECTION
#undef LPCRITICAL_SECTION
#define LPCRITICAL_SECTION WINDOWS_LPCRITICAL_SECTION
#undef WINDOWS_LPCRITICAL_SECTION
#endif

#else
/* Unix/Linux - just include wdsp.h directly */
#include "../wdsp/wdsp.h"
#endif /* _WIN32 */

#endif /* _WDSP_WRAPPER_H_ */
