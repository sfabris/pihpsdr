/* netinet/ip.h replacement for Windows */
#ifndef _WINDOWS_NETINET_IP_H
#define _WINDOWS_NETINET_IP_H

#ifdef _WIN32
#include "../windows_compat.h"

/* IP header structure for Windows */
struct iphdr {
    unsigned int ihl:4;
    unsigned int version:4;
    unsigned char tos;
    unsigned short tot_len;
    unsigned short id;
    unsigned short frag_off;
    unsigned char ttl;
    unsigned char protocol;
    unsigned short check;
    unsigned int saddr;
    unsigned int daddr;
};

/* IP protocol constants */
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif

#else
#include_next <netinet/ip.h>
#endif

#endif
