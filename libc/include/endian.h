#ifndef _ENDIAN_H
#define _ENDIAN_H

#include <stdint.h>

// i386 is little endian. The names follow glibc, which is what code written for Linux expects.
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __PDP_ENDIAN    3412

#define __BYTE_ORDER       __LITTLE_ENDIAN
#define __FLOAT_WORD_ORDER __BYTE_ORDER

#define LITTLE_ENDIAN __LITTLE_ENDIAN
#define BIG_ENDIAN    __BIG_ENDIAN
#define PDP_ENDIAN    __PDP_ENDIAN
#define BYTE_ORDER    __BYTE_ORDER

// host <-> big/little endian conversions
#define htobe16(x) ((uint16_t)__builtin_bswap16((uint16_t)(x)))
#define htole16(x) ((uint16_t)(x))
#define be16toh(x) ((uint16_t)__builtin_bswap16((uint16_t)(x)))
#define le16toh(x) ((uint16_t)(x))

#define htobe32(x) ((uint32_t)__builtin_bswap32((uint32_t)(x)))
#define htole32(x) ((uint32_t)(x))
#define be32toh(x) ((uint32_t)__builtin_bswap32((uint32_t)(x)))
#define le32toh(x) ((uint32_t)(x))

#define htobe64(x) ((uint64_t)__builtin_bswap64((uint64_t)(x)))
#define htole64(x) ((uint64_t)(x))
#define be64toh(x) ((uint64_t)__builtin_bswap64((uint64_t)(x)))
#define le64toh(x) ((uint64_t)(x))

#endif