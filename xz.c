#include <stdio.h>
#include <stdlib.h>

#include "lzma.h"


lzma_ret setup_encoder(lzma_stream *stream);

int main(void) {
    size_t chunk = 1024 * 1024;
    
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret err = setup_encoder(&stream);
    if (LZMA_OK != err) {
        fprintf(stderr, "Can't initialize encoder, error #%d.\n", err);
        exit(1);
    }
    
    uint8_t inbuf[chunk], outbuf[chunk];
    
    stream.avail_in = 0;
    size_t io;
    do {
        stream.next_out = outbuf;
        stream.avail_out = chunk;
        
        lzma_action action;
        if (stream.avail_in == 0) {
            io = fread(inbuf, 1, chunk, stdin);
            if (ferror(stdin) || (!feof(stdin) && chunk != io)) {
                fprintf(stderr, "Read error\n");
                exit(1);
            }
            stream.next_in = inbuf;
            stream.avail_in = io;
            action = feof(stdin) ? LZMA_FINISH : LZMA_RUN;
        }
    
        err = lzma_code(&stream, action);
        if (LZMA_OK != err && LZMA_STREAM_END != err) {
            fprintf(stderr, "Error #%d while encoding LZMA.\n", err);
            exit(1);
        }
    
        size_t write_bytes = chunk - stream.avail_out;
        io = fwrite(outbuf, 1, write_bytes, stdout);
        if (io != write_bytes) {
            fprintf(stderr, "Write error\n");
            exit(1);
        }
    } while (LZMA_STREAM_END != err);
    
    return 0;
}

lzma_ret setup_encoder(lzma_stream *stream) {
    lzma_options_lzma *opts = malloc(sizeof(lzma_options_lzma));
    lzma_lzma_preset(opts, LZMA_PRESET_DEFAULT);
    
    lzma_filter *filters = malloc(2 * sizeof(lzma_filter));
    filters[0].id = LZMA_FILTER_LZMA2;
    filters[0].options = opts;
    filters[1].id = LZMA_VLI_UNKNOWN;
    
    return lzma_stream_encoder(stream, filters, LZMA_CHECK_CRC32);
}
