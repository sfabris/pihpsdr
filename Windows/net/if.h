/* net/if.h replacement for Windows */
#ifndef _WINDOWS_NET_IF_H
#define _WINDOWS_NET_IF_H

#ifdef _WIN32
#include "../windows_compat.h"

/* Network interface definitions for Windows */
#define IFNAMSIZ 16

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct sockaddr ifr_addr;
        struct sockaddr ifr_dstaddr;
        struct sockaddr ifr_broadaddr;
        struct sockaddr ifr_netmask;
        struct sockaddr ifr_hwaddr;
        short ifr_flags;
        int ifr_ifindex;
        int ifr_metric;
        int ifr_mtu;
    };
};

/* Interface flags - basic definitions */
#ifndef IFF_UP
#define IFF_UP          0x1
#endif
#ifndef IFF_BROADCAST
#define IFF_BROADCAST   0x2
#endif
#ifndef IFF_DEBUG
#define IFF_DEBUG       0x4
#endif
#ifndef IFF_LOOPBACK
#define IFF_LOOPBACK    0x8
#endif
#ifndef IFF_POINTOPOINT
#define IFF_POINTOPOINT 0x10
#endif
#ifndef IFF_RUNNING
#define IFF_RUNNING     0x40
#endif
#ifndef IFF_NOARP
#define IFF_NOARP       0x80
#endif
#ifndef IFF_PROMISC
#define IFF_PROMISC     0x100
#endif
#ifndef IFF_ALLMULTI
#define IFF_ALLMULTI    0x200
#endif
#ifndef IFF_MULTICAST
#define IFF_MULTICAST   0x1000
#endif

/* ioctl commands - basic definitions */
#define SIOCGIFFLAGS    0x8913
#define SIOCGIFADDR     0x8915
#define SIOCGIFNETMASK  0x891b
#define SIOCGIFHWADDR   0x8927

#else
#include_next <net/if.h>
#endif

#endif
