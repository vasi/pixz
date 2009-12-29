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
