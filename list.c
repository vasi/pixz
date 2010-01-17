#include "pixz.h"

#include <getopt.h>


#pragma mark FUNCTION DEFINITIONS

int main(int argc, char **argv) {
    char *progname = argv[0];
    int ch;
    bool tar = false;
    while ((ch = getopt(argc, argv, "t")) != -1) {
        switch (ch) {
            case 't':
                tar = true;
                break;
            default:
                die("Unknown option");
        }
    }
    argc -= optind - 1;
    argv += optind - 1;
    
    if (argc != 2)
        die("Usage: %s [-t] file", progname);
    if (!(gInFile = fopen(argv[1], "r")))
        die("Can't open input file");
    
    decode_index();
    lzma_index_record rec;
    while (!lzma_index_read(gIndex, &rec)) {
        fprintf(stderr, "%9"PRIuMAX" / %9"PRIuMAX"\n", (uintmax_t)rec.unpadded_size,
            (uintmax_t)rec.uncompressed_size);
    }
    
    if (tar) {
        fprintf(stderr, "\n");
        read_file_index();
        dump_file_index();
        free_file_index();
    }
    
    lzma_index_end(gIndex, NULL);
    lzma_end(&gStream);
    
    return 0;
}
