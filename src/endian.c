#ifdef __APPLE__

#include <libkern/OSByteOrder.h>

uint64_t xle64dec(const uint8_t *d) {
    return OSReadLittleInt64(d, 0);
}

void xle64enc(uint8_t *d, uint64_t n) {
    OSWriteLittleInt64(d, 0, n);
}

#elif defined(__linux__) || defined(__FreeBSD__)

#include "config.h"
#include <stdint.h>
#include <string.h>
#ifdef __linux__
	#include <endian.h>
	#include <byteswap.h>
#else
	#include <sys/endian.h>
#endif

#if !HAVE_DECL_HTOLE64
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define htole64(x) (x)
# else
#  define htole64(x) bswap_64 (x)
# endif
#endif

#if !HAVE_DECL_LE64TOH
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define le64toh(x) (x)
# else
#  define le64toh(x) bswap_64 (x)
# endif
#endif

uint64_t xle64dec(const uint8_t *d) {
    uint64_t tmp;
    memcpy(&tmp, d, sizeof(tmp));
    return le64toh(tmp);
}

void xle64enc(uint8_t *d, uint64_t n) {
    uint64_t tmp = htole64(n);
    memcpy(d, &tmp, sizeof(tmp));
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
