#ifndef _WINDOWS_SYS_MMAN_H_
#define _WINDOWS_SYS_MMAN_H_

/*
 * Windows compatibility wrapper for sys/mman.h
 * This provides basic memory mapping functionality using Windows APIs
 */

#ifdef _WIN32

#include <windows.h>
#include <io.h>

/* Memory protection flags */
#ifndef PROT_NONE
#define PROT_NONE       0x0     /* No access */
#endif
#ifndef PROT_READ
#define PROT_READ       0x1     /* Read access */
#endif
#ifndef PROT_WRITE
#define PROT_WRITE      0x2     /* Write access */
#endif
#ifndef PROT_EXEC
#define PROT_EXEC       0x4     /* Execute access */
#endif

/* Memory mapping flags */
#ifndef MAP_SHARED
#define MAP_SHARED      0x01    /* Share changes */
#endif
#ifndef MAP_PRIVATE
#define MAP_PRIVATE     0x02    /* Changes are private */
#endif
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS   0x20    /* Anonymous mapping */
#endif
#ifndef MAP_ANON
#define MAP_ANON        MAP_ANONYMOUS
#endif
#ifndef MAP_FAILED
#define MAP_FAILED      ((void *) -1)
#endif

/* Memory sync flags */
#ifndef MS_ASYNC
#define MS_ASYNC        1       /* Asynchronous sync */
#endif
#ifndef MS_SYNC
#define MS_SYNC         4       /* Synchronous sync */
#endif
#ifndef MS_INVALIDATE
#define MS_INVALIDATE   2       /* Invalidate mappings */
#endif

/* Basic mmap implementation using Windows CreateFileMapping/MapViewOfFile */
static inline void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMapping = NULL;
    DWORD flProtect = 0;
    DWORD dwDesiredAccess = 0;
    void *result = MAP_FAILED;
    
    (void)addr; /* addr parameter is ignored on Windows */
    
    /* Convert protection flags */
    if (prot & PROT_WRITE) {
        if (prot & PROT_EXEC) {
            flProtect = PAGE_EXECUTE_READWRITE;
            dwDesiredAccess = FILE_MAP_ALL_ACCESS;
        } else {
            flProtect = PAGE_READWRITE;
            dwDesiredAccess = FILE_MAP_ALL_ACCESS;
        }
    } else if (prot & PROT_READ) {
        if (prot & PROT_EXEC) {
            flProtect = PAGE_EXECUTE_READ;
            dwDesiredAccess = FILE_MAP_READ | FILE_MAP_EXECUTE;
        } else {
            flProtect = PAGE_READONLY;
            dwDesiredAccess = FILE_MAP_READ;
        }
    } else {
        flProtect = PAGE_NOACCESS;
        dwDesiredAccess = 0;
    }
    
    if (flags & MAP_ANONYMOUS) {
        /* Anonymous mapping */
        hMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, flProtect, 
                                    (DWORD)(length >> 32), (DWORD)length, NULL);
    } else {
        /* File mapping */
        hFile = (HANDLE)_get_osfhandle(fd);
        if (hFile == INVALID_HANDLE_VALUE) {
            return MAP_FAILED;
        }
        hMapping = CreateFileMapping(hFile, NULL, flProtect, 
                                    (DWORD)(length >> 32), (DWORD)length, NULL);
    }
    
    if (hMapping == NULL) {
        return MAP_FAILED;
    }
    
    result = MapViewOfFile(hMapping, dwDesiredAccess, 
                          (DWORD)(offset >> 32), (DWORD)offset, length);
    
    CloseHandle(hMapping);
    
    return result ? result : MAP_FAILED;
}

/* munmap implementation using Windows UnmapViewOfFile */
static inline int munmap(void *addr, size_t length) {
    (void)length; /* length is ignored on Windows */
    return UnmapViewOfFile(addr) ? 0 : -1;
}

/* msync implementation using Windows FlushViewOfFile */
static inline int msync(void *addr, size_t length, int flags) {
    (void)flags; /* flags are ignored for simplicity */
    return FlushViewOfFile(addr, length) ? 0 : -1;
}

/* mprotect stub - not easily implementable on Windows */
static inline int mprotect(void *addr, size_t len, int prot) {
    (void)addr;
    (void)len;
    (void)prot;
    /* Not implemented - return success for compatibility */
    return 0;
}

#else
/* Unix/Linux - use system sys/mman.h */
#include <sys/mman.h>
#endif /* _WIN32 */

#endif /* _WINDOWS_SYS_MMAN_H_ */
