#ifndef _WINDOWS_PWD_H_
#define _WINDOWS_PWD_H_

/*
 * Windows compatibility wrapper for pwd.h
 * This header provides user database access functions
 */

#ifdef _WIN32

#include <windows.h>
#include <lmcons.h>

/* Password file entry structure */
struct passwd {
    char *pw_name;      /* username */
    char *pw_passwd;    /* user password */
    uid_t pw_uid;       /* user ID */
    gid_t pw_gid;       /* group ID */
    char *pw_gecos;     /* user information */
    char *pw_dir;       /* home directory */
    char *pw_shell;     /* shell program */
};

/* Define uid_t and gid_t if not already defined */
#ifndef _UID_T_DEFINED
#define _UID_T_DEFINED
typedef unsigned int uid_t;
#endif

#ifndef _GID_T_DEFINED
#define _GID_T_DEFINED
typedef unsigned int gid_t;
#endif

/* Function declarations */
struct passwd *getpwuid(uid_t uid);
uid_t getuid(void);
gid_t getgid(void);

#else
/* Unix/Linux - use system pwd.h */
#include <pwd.h>
#endif /* _WIN32 */

#endif /* _WINDOWS_PWD_H_ */
