/* Copyright (C)
* 2025 - Windows/MinGW port for piHPSDR
*
* This header is now unused - we use header wrapper files instead
*/

#ifndef _WINDOWS_AUTO_COMPAT_H_
#define _WINDOWS_AUTO_COMPAT_H_

/* This file is kept for compatibility but no longer used */
/* Header wrapper files in Windows/ subdirectories handle compatibility */

#endif /* _WINDOWS_AUTO_COMPAT_H_ */

/* Copyright (C)
* 2025 - Windows/MinGW port for piHPSDR
*
* This header provides automatic Windows compatibility
* when building on Windows platforms.
*/

#ifndef _WINDOWS_AUTO_COMPAT_H_
#define _WINDOWS_AUTO_COMPAT_H_

#ifdef _WIN32
/* Automatically include Windows compatibility layer */
#include "windows_compat.h"

/* Override problematic includes */
#ifdef _SEMAPHORE_H
#undef _SEMAPHORE_H
#endif

#ifdef _PTHREAD_H
#undef _PTHREAD_H
#endif

/* Prevent inclusion of POSIX headers that we replace */
#define _SEMAPHORE_H 1
#define _PTHREAD_H 1

#endif /* _WIN32 */

#endif /* _WINDOWS_AUTO_COMPAT_H_ */
