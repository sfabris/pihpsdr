#ifndef _WINDOWS_FCNTL_H_
#define _WINDOWS_FCNTL_H_

/*
 * Windows compatibility wrapper for fcntl.h
 * This provides file control operations compatibility
 */

#ifdef _WIN32

#include <windows.h>
#include <io.h>

/* File access modes */
#ifndef O_RDONLY
#define O_RDONLY    0x0000
#endif
#ifndef O_WRONLY
#define O_WRONLY    0x0001
#endif
#ifndef O_RDWR
#define O_RDWR      0x0002
#endif

/* File creation flags */
#ifndef O_CREAT
#define O_CREAT     0x0040
#endif
#ifndef O_EXCL
#define O_EXCL      0x0080
#endif
#ifndef O_TRUNC
#define O_TRUNC     0x0200
#endif
#ifndef O_APPEND
#define O_APPEND    0x0008
#endif

/* File status flags */
#ifndef O_NONBLOCK
#define O_NONBLOCK  0x0004
#endif

/* POSIX flags not available on Windows - define as no-op */
#define O_NOCTTY    0x0000  /* No controlling terminal */
#define O_SYNC      0x0000  /* Synchronous writes (no-op on Windows) */

/* fcntl commands */
#define F_DUPFD     0
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4

/* fcntl flags */
#define FD_CLOEXEC  1

/* Function declarations */
int fcntl(int fd, int cmd, ...);

#else
/* Unix/Linux - use system fcntl.h */
#include <fcntl.h>
#endif /* _WIN32 */

#endif /* _WINDOWS_FCNTL_H_ */
