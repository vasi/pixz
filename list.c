#include "pixz.h"

#pragma mark FUNCTION DEFINITIONS

void pixz_list(bool tar) {
    if (!decode_index())
		die("Can't list non-seekable input");
	
    lzma_index_iter iter;
    lzma_index_iter_init(&iter, gIndex);

    if (tar && read_file_index()) {
        dump_file_index(stdout, false);
        free_file_index();
    } else {
        while (!lzma_index_iter_next(&iter, LZMA_INDEX_ITER_BLOCK)) {
            printf("%9"PRIuMAX" / %9"PRIuMAX"\n",
                (uintmax_t)iter.block.unpadded_size,
                (uintmax_t)iter.block.uncompressed_size);
        }
    }
    
    lzma_index_end(gIndex, NULL);
    lzma_end(&gStream);
}
