#include "pixz.h"

#include <stdarg.h>

void pixz_die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fflush(stderr);
    va_end(args);
    exit(1);
}

// FIXME: Portability
#include <libkern/OSByteOrder.h>

void pixz_offset_write(uint64_t n, uint8_t *buf) {
    OSWriteLittleInt64(buf, 0, n);
}
