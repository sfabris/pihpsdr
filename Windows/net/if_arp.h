#ifndef _WINDOWS_NET_IF_ARP_H_
#define _WINDOWS_NET_IF_ARP_H_

/*
 * Windows compatibility wrapper for net/if_arp.h
 * This header contains ARP (Address Resolution Protocol) definitions
 */

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

/* ARP hardware types */
#ifndef ARPHRD_ETHER
#define ARPHRD_ETHER    1       /* Ethernet */
#endif

#ifndef ARPHRD_IEEE802
#define ARPHRD_IEEE802  6       /* IEEE 802.2 Ethernet/TR/TB */
#endif

#ifndef ARPHRD_ARCNET
#define ARPHRD_ARCNET   7       /* ARCnet */
#endif

#ifndef ARPHRD_ATM
#define ARPHRD_ATM      19      /* ATM */
#endif

#ifndef ARPHRD_IEEE80211
#define ARPHRD_IEEE80211 801    /* IEEE 802.11 */
#endif

#ifndef ARPHRD_IEEE80211_PRISM
#define ARPHRD_IEEE80211_PRISM 802 /* IEEE 802.11 + Prism2 header */
#endif

#ifndef ARPHRD_IEEE80211_RADIOTAP
#define ARPHRD_IEEE80211_RADIOTAP 803 /* IEEE 802.11 + radiotap header */
#endif

/* ARP protocol opcodes */
#ifndef ARPOP_REQUEST
#define ARPOP_REQUEST   1       /* ARP request */
#endif

#ifndef ARPOP_REPLY
#define ARPOP_REPLY     2       /* ARP reply */
#endif

#ifndef ARPOP_RREQUEST
#define ARPOP_RREQUEST  3       /* RARP request */
#endif

#ifndef ARPOP_RREPLY
#define ARPOP_RREPLY    4       /* RARP reply */
#endif

/* ARP header structure */
struct arphdr {
    unsigned short ar_hrd;      /* Hardware type */
    unsigned short ar_pro;      /* Protocol type */
    unsigned char  ar_hln;      /* Hardware address length */
    unsigned char  ar_pln;      /* Protocol address length */
    unsigned short ar_op;       /* ARP opcode */
};

#else
/* Unix/Linux - use system net/if_arp.h */
#include <net/if_arp.h>
#endif /* _WIN32 */

#endif /* _WINDOWS_NET_IF_ARP_H_ */
