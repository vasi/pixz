#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "lzma.h"


#define DEFAULT_CHUNKSIZE (1024 * 1024)


typedef struct {
    size_t (*read)(void *data, uint8_t *buf, size_t size, int *eofp);
    void *data;
} reader;

typedef struct {
    // Return non-zero on error
    size_t (*write)(void *data, const uint8_t *buf, size_t size);
    void *data;
} writer;

typedef struct {
    reader rd;
    writer wr;
    size_t chunksize;
    lzma_stream_flags stream_flags;
    
    lzma_filter filters[LZMA_FILTERS_MAX + 1];
} encode_options;


void die(const char *fmt, ...);

void file_reader(reader *rd, FILE *file);
size_t file_read(void *data, uint8_t *buf, size_t size, int *eofp);
void file_writer(writer *wr, FILE *file);
size_t file_write(void *data, const uint8_t *buf, size_t size);

void encode_stream_end(encode_options *eo, lzma_vli backward_size);
void encode_file(FILE *in, FILE *out);
void encode_stream_header(encode_options *eo);
void encode_block(encode_options *eo, lzma_block *block);
lzma_vli encode_index(encode_options *eo, lzma_block *blocks, size_t nblocks);

void encode(lzma_stream *stream, size_t chunksize, reader *rd, writer *wr);

int main(void) {
    encode_file(stdin, stdout);
    return 0;
}

void die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fflush(stderr);
    va_end(args);
    exit(1);
}


void encode_file(FILE *in, FILE *out) {
    lzma_options_lzma filt_options;
    if (lzma_lzma_preset(&filt_options, LZMA_PRESET_DEFAULT))
        die("Error determining filter options.\n");
    
    encode_options eo = {
        .chunksize = DEFAULT_CHUNKSIZE,
        .stream_flags = { .version = 0, .check = LZMA_CHECK_CRC32 },
        .filters = {
            { .id = LZMA_FILTER_LZMA2, .options = &filt_options },
            { .id = LZMA_VLI_UNKNOWN }
        }
    };
    file_reader(&eo.rd, in);
    file_writer(&eo.wr, out);
    
    encode_stream_end(&eo, LZMA_VLI_UNKNOWN);
    lzma_block block;    
    encode_block(&eo, &block);
    lzma_vli backward = encode_index(&eo, &block, 1);
    encode_stream_end(&eo, backward);
}

void file_reader(reader *rd, FILE *file) {
    rd->read = &file_read;
    rd->data = file;
}

size_t file_read(void *data, uint8_t *buf, size_t size, int *eofp) {
    FILE *file = (FILE*)data;
    size_t io = fread(buf, 1, size, file);
    
    if (eofp)
        *eofp = feof(file);
    return io;
}

void file_writer(writer *wr, FILE *file) {
    wr->write = &file_write;
    wr->data = file;
}

size_t file_write(void *data, const uint8_t *buf, size_t size) {
    FILE *file = (FILE*)data;
    return fwrite(buf, 1, size, file);
}

void encode_block(encode_options *eo, lzma_block *block) {
    block->version = 0;
    block->check = eo->stream_flags.check;
    block->filters = eo->filters;
    block->compressed_size = block->uncompressed_size = LZMA_VLI_UNKNOWN;
    
    // Write the header
    lzma_ret err = lzma_block_header_size(block);
    if (LZMA_OK != err)
        die("Error #%d determining size of block header.\n", err);
    uint8_t header[block->header_size];
    err = lzma_block_header_encode(block, header);
    if (LZMA_OK != err)
        die("Error #%d encoding block header.\n", err);
    size_t wr = eo->wr.write(eo->wr.data, header, block->header_size);
    if (wr != LZMA_STREAM_HEADER_SIZE)
        die("Error writing stream header.\n");
    
    // Write the data
    lzma_stream stream = LZMA_STREAM_INIT;
    err = lzma_block_encoder(&stream, block);
    if (err)
        die("Error #%d creating block encoder.\n", err);
    encode(&stream, eo->chunksize, &eo->rd, &eo->wr);
}

void encode(lzma_stream *stream, size_t chunksize, reader *rd, writer *wr) {
    uint8_t inbuf[chunksize], outbuf[chunksize];
    
    lzma_ret err = LZMA_OK;
    stream->avail_in = 0;
    stream->next_in = inbuf;
    do {
        stream->next_out = outbuf;
        stream->avail_out = chunksize;
        
        lzma_action action;
        if (rd && stream->avail_in == 0) {
            int eof = 0;
            stream->avail_in = rd->read(rd->data, inbuf, chunksize, &eof);
            if (!eof && stream->avail_in != chunksize)
                die("Read error during encoding.\n");
            stream->next_in = inbuf;
            action = eof ? LZMA_FINISH : LZMA_RUN;
        }
    
        err = lzma_code(stream, action);
        if (LZMA_OK != err && LZMA_STREAM_END != err)
            die("Error #%d while encoding.\n", err);
    
        size_t write_bytes = chunksize - stream->avail_out;
        if (wr->write(wr->data, outbuf, write_bytes) != write_bytes)
            die("Write error during encoding.\n");
    } while (LZMA_STREAM_END != err);
}

lzma_vli encode_index(encode_options *eo, lzma_block *blocks, size_t nblocks) {
    lzma_ret err;
    lzma_index *index = lzma_index_init(NULL, NULL);
    for (int i = 0; i < nblocks; ++i) {
        err = lzma_index_append(index, NULL,
            lzma_block_unpadded_size(&blocks[i]),
            blocks[i].uncompressed_size);
        if (LZMA_OK != err)
            die("Error #%d appending record to index.\n", err);
    }
    
    lzma_stream stream = LZMA_STREAM_INIT;
    err = lzma_index_encoder(&stream, index);
    if (LZMA_OK != err)
        die("Error #%d creating index encoder.\n", err);
    
    encode(&stream, eo->chunksize, NULL, &eo->wr);
    
    lzma_vli size = lzma_index_size(index);
    lzma_index_end(index, NULL);
    return size;
}

void encode_stream_end(encode_options *eo, lzma_vli backward_size) {
    uint8_t buf[LZMA_STREAM_HEADER_SIZE];
    eo->stream_flags.backward_size = backward_size;
    
    lzma_ret err = (backward_size == LZMA_VLI_UNKNOWN
        ? &lzma_stream_header_encode
        : &lzma_stream_footer_encode
    )(&eo->stream_flags, buf);
    if (LZMA_OK != err)
        die("Error #%d encoding stream end.\n", err);
    size_t wr = eo->wr.write(eo->wr.data, buf, LZMA_STREAM_HEADER_SIZE);
    if (wr != LZMA_STREAM_HEADER_SIZE)
        die("Error writing stream end.\n");
}
