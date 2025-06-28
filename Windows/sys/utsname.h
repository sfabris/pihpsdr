#ifndef _WINDOWS_SYS_UTSNAME_H_
#define _WINDOWS_SYS_UTSNAME_H_

/*
 * Windows compatibility wrapper for sys/utsname.h
 * Provides uname() functionality for Windows
 */

#ifdef _WIN32

#include <windows.h>

/* Maximum length for utsname fields */
#define _UTSNAME_LENGTH 65

struct utsname {
    char sysname[_UTSNAME_LENGTH];   /* Operating system name */
    char nodename[_UTSNAME_LENGTH];  /* Network node hostname */
    char release[_UTSNAME_LENGTH];   /* Operating system release */
    char version[_UTSNAME_LENGTH];   /* Operating system version */
    char machine[_UTSNAME_LENGTH];   /* Hardware type */
};

/* Function declaration */
int uname(struct utsname *buf);

#else
/* Unix/Linux - use system header */
#include <sys/utsname.h>
#endif /* _WIN32 */

#endif /* _WINDOWS_SYS_UTSNAME_H_ */
