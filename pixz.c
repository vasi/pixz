#include "pixz.h"

#include <stdarg.h>

int main(void) {
    pixz_encode_options *opts = pixz_encode_options_new();
    pixz_encode_options_default(opts);
    
    pixz_encode_file(stdin, stdout, opts);
    
    pixz_encode_options_free(opts);
        
    return 0;
}

void pixz_die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fflush(stderr);
    va_end(args);
    exit(1);
}
