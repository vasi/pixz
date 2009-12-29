#include "pixz.h"

int main(void) {
    pixz_encode_options *opts = pixz_encode_options_new();
    pixz_encode_options_default(opts);
    
    pixz_encode_file(stdin, stdout, opts);
    
    pixz_encode_options_free(opts);
        
    return 0;
}
