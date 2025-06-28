#ifndef _WINDOWS_IFADDRS_H_
#define _WINDOWS_IFADDRS_H_

/*
 * Windows compatibility wrapper for ifaddrs.h
 * This provides network interface enumeration functionality
 */

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>

/* Network interface address structure */
struct ifaddrs {
    struct ifaddrs  *ifa_next;    /* Next item in list */
    char            *ifa_name;    /* Name of interface */
    unsigned int     ifa_flags;   /* Flags from SIOCGIFFLAGS */
    struct sockaddr *ifa_addr;    /* Address of interface */
    struct sockaddr *ifa_netmask; /* Netmask of interface */
    union {
        struct sockaddr *ifu_broadaddr; /* Broadcast address of interface */
        struct sockaddr *ifu_dstaddr;   /* Point-to-point destination address */
    } ifa_ifu;
    void            *ifa_data;    /* Address-specific data */
};

#define ifa_broadaddr ifa_ifu.ifu_broadaddr
#define ifa_dstaddr   ifa_ifu.ifu_dstaddr

/* Interface flags (from Linux) */
#ifndef IFF_UP
#define IFF_UP          0x1     /* Interface is up */
#endif
#ifndef IFF_BROADCAST
#define IFF_BROADCAST   0x2     /* Broadcast address valid */
#endif
#ifndef IFF_DEBUG
#define IFF_DEBUG       0x4     /* Turn on debugging */
#endif
#ifndef IFF_LOOPBACK
#define IFF_LOOPBACK    0x8     /* Is a loopback net */
#endif
#ifndef IFF_POINTOPOINT
#define IFF_POINTOPOINT 0x10    /* Interface is point-to-point link */
#endif
#ifndef IFF_NOTRAILERS
#define IFF_NOTRAILERS  0x20    /* Avoid use of trailers */
#endif
#ifndef IFF_RUNNING
#define IFF_RUNNING     0x40    /* Resources allocated */
#endif
#ifndef IFF_NOARP
#define IFF_NOARP       0x80    /* No address resolution protocol */
#endif
#ifndef IFF_PROMISC
#define IFF_PROMISC     0x100   /* Receive all packets */
#endif
#ifndef IFF_ALLMULTI
#define IFF_ALLMULTI    0x200   /* Receive all multicast packets */
#endif
#ifndef IFF_MULTICAST
#define IFF_MULTICAST   0x1000  /* Supports multicast */
#endif

/* Function declarations */
int getifaddrs(struct ifaddrs **ifap);
void freeifaddrs(struct ifaddrs *ifa);

#else
/* Unix/Linux - use system ifaddrs.h */
#include <ifaddrs.h>
#endif /* _WIN32 */

#endif /* _WINDOWS_IFADDRS_H_ */
