/* endian.h replacement for Windows */
#ifndef _WINDOWS_ENDIAN_H
#define _WINDOWS_ENDIAN_H

#ifdef _WIN32
#include <winsock2.h>

/* Byte order definitions */
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

/* Host to network byte order conversion */
#ifndef htobe16
#define htobe16(x) htons(x)
#endif
#ifndef htole16
#define htole16(x) (x)
#endif
#ifndef be16toh
#define be16toh(x) ntohs(x)
#endif
#ifndef le16toh
#define le16toh(x) (x)
#endif

#ifndef htobe32
#define htobe32(x) htonl(x)
#endif
#ifndef htole32
#define htole32(x) (x)
#endif
#ifndef be32toh
#define be32toh(x) ntohl(x)
#endif
#ifndef le32toh
#define le32toh(x) (x)
#endif

/* 64-bit byte order conversion */
#ifndef htobe64
#ifdef _MSC_VER
#define htobe64(x) _byteswap_uint64(x)
#else
#define htobe64(x) __builtin_bswap64(x)
#endif
#endif

#ifndef htole64
#define htole64(x) (x)
#endif

#ifndef be64toh
#ifdef _MSC_VER
#define be64toh(x) _byteswap_uint64(x)
#else
#define be64toh(x) __builtin_bswap64(x)
#endif
#endif

#ifndef le64toh
#define le64toh(x) (x)
#endif

#else
#include_next <endian.h>
#endif

#endif
