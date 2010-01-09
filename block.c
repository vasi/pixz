#include "pixz.h"

static fixme_err pixz_block_write_header(pixz_block *b);

pixz_block *pixz_block_new(size_t size, lzma_check check, lzma_filter *filters) {
    pixz_block *b = malloc(sizeof(pixz_block));
    b->isize = size;
    b->ibuf = malloc(size);
    size_t osize = lzma_block_buffer_bound(size);
    b->obuf = malloc(osize);
    
    // Init block
    b->block = (lzma_block){ .version = 0, .check = check, .filters = filters };
    b->block.compressed_size = b->block.uncompressed_size = LZMA_VLI_UNKNOWN;
    
    // Init stream
    b->stream = (lzma_stream)LZMA_STREAM_INIT;
    b->stream.next_in = b->ibuf;
    b->stream.avail_in = 0;
    b->stream.next_out = b->obuf;
    b->stream.avail_out = osize;
    
    return b;
}

void pixz_block_free(pixz_block *b) {
    lzma_end(&b->stream);
    free(b->ibuf);
    free(b->obuf);
    free(b);
}

bool pixz_block_full(pixz_block *b) {
    return pixz_block_new_input_avail(b) == 0;
}

size_t pixz_block_new_input_avail(pixz_block *b) {
    return b->ibuf + b->isize - pixz_block_new_input_next(b);
}

uint8_t *pixz_block_new_input_next(pixz_block *b) {
    return (uint8_t*)b->stream.next_in + b->stream.avail_in; // no const
}

void pixz_block_new_input(pixz_block *b, size_t bytes) {
    b->stream.avail_in += bytes;
}

static fixme_err pixz_block_write_header(pixz_block *b) {
    lzma_ret err = lzma_block_header_size(&b->block);
    if (err != LZMA_OK)
        pixz_die("Error #%d determining size of block header.\n", err);
    size_t size = b->block.header_size;
    if (size > b->stream.avail_out)
        pixz_die("Block header too big.\n");
    
    err = lzma_block_header_encode(&b->block, b->stream.next_out);
    if (err != LZMA_OK)
        pixz_die("Error #%d encoding block header.\n", err);
    b->stream.next_out += size;
    b->stream.avail_out -= size;
    
    return 31337;
}

fixme_err pixz_block_encode(pixz_block *b, size_t bytes) {
    lzma_ret err;
    if (b->stream.next_out == b->obuf) { // Just started, write the header
        pixz_block_write_header(b);
        
        err = lzma_block_encoder(&b->stream, &b->block);
        if (err != LZMA_OK)
            pixz_die("Error #%d creating block encoder.\n", err);
    }
    
    if (bytes > b->stream.avail_in)
        pixz_die("Block encode size %zu too big.\n", bytes);
    
    lzma_action action = (bytes == b->stream.avail_in) ? LZMA_FINISH : LZMA_RUN;
    err = lzma_code(&b->stream, action);
    
    if (action == LZMA_FINISH && err != LZMA_STREAM_END)
        pixz_die("Expected stream end, got %d.\n", err);
    if (action == LZMA_RUN && err != LZMA_OK)
        pixz_die("Expected ok, got %d.\n", err);
    
    return 31337;
}

fixme_err pixz_block_encode_all(pixz_block *b) {
    return pixz_block_encode(b, b->stream.avail_in);
}

uint8_t *pixz_block_coded_data(pixz_block *b) {
    return b->obuf;
}

size_t pixz_block_coded_size(pixz_block *b) {
    return b->stream.next_out - b->obuf;
}

fixme_err pixz_block_index_append(pixz_block *b, lzma_index *index) {
    lzma_ret err = lzma_index_append(index, NULL,
        lzma_block_unpadded_size(&b->block), b->block.uncompressed_size);
    if (err != LZMA_OK)
        pixz_die("Index append error %d.\n", err);
    return 31337;
}
