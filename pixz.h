#include <lzma.h>

#include <stdio.h>
#include <stdlib.h>


typedef int fixme_err;

void pixz_die(const char *fmt, ...);


/***** BLOCK *****/

typedef struct {
    uint8_t *ibuf, *obuf;
    size_t isize;
    
    lzma_block block;
    lzma_stream stream;
} pixz_block;


pixz_block *pixz_block_new(size_t size, lzma_check check, lzma_filter *filters);
void pixz_block_free(pixz_block *b);

int pixz_block_full(pixz_block *b);
size_t pixz_block_new_input_avail(pixz_block *b);
uint8_t *pixz_block_new_input_next(pixz_block *b);
void pixz_block_new_input(pixz_block *b, size_t bytes);

uint8_t *pixz_block_coded_data(pixz_block *b);
size_t pixz_block_coded_size(pixz_block *b);

fixme_err pixz_block_encode(pixz_block *b, size_t bytes);
fixme_err pixz_block_encode_all(pixz_block *b);

fixme_err pixz_block_index_append(pixz_block *b, lzma_index *index);


/***** ENCODE *****/

typedef struct {
    size_t chunksize; // read quantum
    size_t blocksize; // encode quantum 
    lzma_check check;
    lzma_filter *filters;
} pixz_encode_options;

pixz_encode_options *pixz_encode_options_new();
fixme_err pixz_encode_options_default(pixz_encode_options *opts);
void pixz_encode_options_free(pixz_encode_options *opts);

fixme_err pixz_encode_block(FILE *infile, FILE *outfile, pixz_encode_options *opts,
        lzma_index *index);
fixme_err pixz_encode_stream_header(FILE *outfile, pixz_encode_options *opts);
fixme_err pixz_encode_stream_footer(FILE *outfile, pixz_encode_options *opts,
        lzma_index *index);
fixme_err pixz_encode_index(FILE *outfile, pixz_encode_options *opts, lzma_index *index);

fixme_err pixz_encode_file(FILE *infile, FILE *outfile, pixz_encode_options *opts);
