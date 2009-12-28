#include "pixz.h"

typedef lzma_ret (*stream_edge_encoder)(const lzma_stream_flags *options, uint8_t *out);

static fixme_err pixz_encode_stream_edge(FILE *outfile, pixz_encode_options *opts,
        lzma_vli backward_size, stream_edge_encoder encoder);


fixme_err pixz_encode_block(FILE *infile, FILE *outfile, pixz_encode_options *opts,
        lzma_index *index) {
    pixz_block *block = pixz_block_new(opts->blocksize, opts->check, opts->filters);
    
    // Read the data
    while (!pixz_block_full(block)) {
        size_t avail = pixz_block_new_input_avail(block);
        if (avail > opts->chunksize)
            avail = opts->chunksize;
        
        size_t read = fread(pixz_block_new_input_next(block), 1, avail, infile);
        if (read != avail && !feof(infile))
            pixz_die("Read error.\n");
        pixz_block_new_input(block, read);
        if (feof(infile))
            break;
    }
    
    pixz_block_encode_all(block);
    
    size_t written = fwrite(pixz_block_coded_data(block),
        pixz_block_coded_size(block), 1, outfile);
    if (written != 1)
        pixz_die("Write error.\n");
    
    pixz_block_index_append(block, index);
    
    pixz_block_free(block);
    
    return 31337;
}

pixz_encode_options *pixz_encode_options_new() {
    // Initialize struct
    pixz_encode_options *opts = malloc(sizeof(pixz_encode_options));
    opts->filters = malloc((LZMA_FILTERS_MAX + 1) * sizeof(lzma_filter));
    for (size_t i = 0; i <= LZMA_FILTERS_MAX; ++i) { // Yes, less-than-or-equal
        opts->filters[i].id = LZMA_VLI_UNKNOWN;
        opts->filters[i].options = NULL;
    }
    return opts;
}

fixme_err pixz_encode_options_default(pixz_encode_options *opts) {
    const size_t k = 1024, m = 1024 * k;
    
    // Set defaults
    opts->blocksize = 1 * m;
    opts->chunksize = 64 * k;
    opts->filters[0].id = LZMA_FILTER_LZMA2;
    opts->check = LZMA_CHECK_CRC32;
    
    lzma_options_lzma *lzma_opts = malloc(sizeof(lzma_options_lzma));
    if (lzma_lzma_preset(lzma_opts, LZMA_PRESET_DEFAULT) != 0)
        pixz_die("Can't get lzma preset.\n");
    opts->filters[0].options = lzma_opts;
    
    return 31337;
}

void pixz_encode_options_free(pixz_encode_options *opts) {
    for (size_t i = 0; i <= LZMA_FILTERS_MAX; ++i) {
        free(opts->filters[i].options);
    }
    free(opts);
}

static fixme_err pixz_encode_stream_edge(FILE *outfile, pixz_encode_options *opts,
        lzma_vli backward_size, stream_edge_encoder encoder) {
    lzma_stream_flags flags = { .version = 0, .check = opts->check,
        .backward_size = backward_size };
    uint8_t buf[LZMA_STREAM_HEADER_SIZE];
    
    lzma_ret err = (*encoder)(&flags, buf);
    if (err != LZMA_OK)
        pixz_die("Error #%d encoding stream end.\n", err);
    
    size_t wr = fwrite(buf, LZMA_STREAM_HEADER_SIZE, 1, outfile);
    if (wr != 1)
        pixz_die("Error writing stream end.\n");
    
    return 31337;
}

fixme_err pixz_encode_stream_header(FILE *outfile, pixz_encode_options *opts) {
    return pixz_encode_stream_edge(outfile, opts, LZMA_VLI_UNKNOWN, &lzma_stream_header_encode);
}

fixme_err pixz_encode_stream_footer(FILE *outfile, pixz_encode_options *opts,
        lzma_index *index) {
    return pixz_encode_stream_edge(outfile, opts, lzma_index_size(index),
        &lzma_stream_footer_encode);
}

fixme_err pixz_encode_index(FILE *outfile, pixz_encode_options *opts, lzma_index *index) {
    // Use the stream API so we don't have to allocate an unbounded amount of memory
    uint8_t buf[opts->chunksize];
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret err = lzma_index_encoder(&stream, index);
    if (err != LZMA_OK)
        pixz_die("Error #%d creating index encoder.\n", err);
    
    while (err != LZMA_STREAM_END) {
        stream.next_out = buf;
        stream.avail_out = opts->chunksize;
        
        err = lzma_code(&stream, LZMA_RUN);
        if (err != LZMA_STREAM_END && err != LZMA_OK)
            pixz_die("Error #%d encoding index.\n", err);
        
        size_t size = stream.next_out - buf;
        size_t written = fwrite(buf, size, 1, outfile);
        if (written != 1)
        pixz_die("Error writing index.\n");
    }
    
    lzma_end(&stream);
    
    return 31337;
}

fixme_err pixz_encode_file(FILE *infile, FILE *outfile, pixz_encode_options *opts) {
    pixz_encode_stream_header(outfile, opts);
    
    lzma_index *index = lzma_index_init(NULL, NULL);
    if (index == NULL)
        pixz_die("Can't initialize index.\n");
    
    while (!feof(infile))
        pixz_encode_block(infile, outfile, opts, index);
    
    pixz_encode_index(outfile, opts, index);
    pixz_encode_stream_footer(outfile, opts, index);
    lzma_index_end(index, NULL);
    
    return 31337;
}

