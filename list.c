#include "pixz.h"

#include <getopt.h>


#pragma mark FUNCTION DEFINITIONS

int main(int argc, char **argv) {
    char *progname = argv[0];
    
    if (argc == 1) {
        gInFile = stdin;
    } else if (argc == 2) {
        if (!(gInFile = fopen(argv[1], "r")))
            die("Can't open input file");
    } else {
        die("Usage: %s [FILE]", progname);
    }
    
    decode_index();
    lzma_index_iter iter;
    lzma_index_iter_init(&iter, gIndex);
    while (!lzma_index_iter_next(&iter, LZMA_INDEX_ITER_BLOCK)) {
        printf("%9"PRIuMAX" / %9"PRIuMAX"\n",
            (uintmax_t)iter.block.unpadded_size,
            (uintmax_t)iter.block.uncompressed_size);
    }
    
    if (read_file_index()) {
        printf("\n");
        dump_file_index(stdout);
        free_file_index();
    }
    
    lzma_index_end(gIndex, NULL);
    lzma_end(&gStream);
    
    return 0;
}
