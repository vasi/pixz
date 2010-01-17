#ifdef __APPLE__

#include <libkern/OSByteOrder.h>

uint64_t xle64dec(const uint8_t *d) {
    return OSReadLittleInt64(d, 0);
}

void xle64enc(uint8_t *d, uint64_t n) {
    OSWriteLittleInt64(d, 0, n);
}


#elif defined(__linux__)

#define _BSD_SOURCE
#include <stdint.h>
#include <endian.h>

uint64_t xle64dec(const uint8_t *d) {
    return le64toh(*(uint64_t*)d);
}

void xle64enc(uint8_t *d, uint64_t n) {
    *(uint64_t*)d = htole64(n);
}


#endif
