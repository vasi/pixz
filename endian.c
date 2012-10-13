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
#include <asm/byteorder.h>

uint64_t xle64dec(const uint8_t *d) {
    return __le64_to_cpu(*(uint64_t*)d);
}

void xle64enc(uint8_t *d, uint64_t n) {
    *(uint64_t*)d = __cpu_to_le64(n);
}

#else

// Platform independent
#include <stdint.h>

uint64_t xle64dec(const uint8_t *d) {
	uint64_t r = 0;
	for (const uint8_t *p = d + sizeof(r) - 1; p >= d; --p)
		r = (r << 8) + *p;
	return r;
}

void xle64enc(uint8_t *d, uint64_t n) {
	for (uint8_t *p = d; p < d + sizeof(n); ++p) {
		*p = n & 0xff;
		n >>= 8;
	}
}

#endif
