#include "pixz.h"

/* TODO
 * - parallel extraction
 * - restrict to certain files
 * - verify file-index matches archive contents
 */

static FILE *gOutFile = NULL;

static size_t largest_block_size();

int main(int argc, char **argv) {
    // TODO: Arguments?
    gInFile = stdin;
    gOutFile = stdout;
    
    // Find largest block size
    size_t blocksize = largest_block_size();
    printf("block size: %zu\n", blocksize);
    
    return 0;
}

static size_t largest_block_size() {
    // exclude the index block
    lzma_vli index_offset = find_file_index(NULL);
    
    lzma_index_iter iter;
    lzma_index_iter_init(&iter, gIndex);
    
    size_t largest = 0;
    while (!lzma_index_iter_next(&iter, LZMA_INDEX_ITER_BLOCK)) {
        if (index_offset && iter.block.compressed_file_offset == index_offset)
            continue;
        if (iter.block.uncompressed_size > largest)
            largest = iter.block.uncompressed_size;
    }
    return largest;
}
