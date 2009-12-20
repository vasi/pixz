#include <stdio.h>
#include <stdlib.h>

#include "lzma.h"

#define CHUNK (1024 * 1024)

int main(void) {
    lzma_stream stream = LZMA_STREAM_INIT;
    
    lzma_ret err = lzma_easy_encoder(&stream, LZMA_PRESET_DEFAULT, LZMA_CHECK_CRC32);
    if (LZMA_OK != err) {
        fprintf(stderr, "Can't initialize encoder, error #%d.\n", err);
        exit(1);
    }
    
    uint8_t inbuf[CHUNK], outbuf[CHUNK];
    
    stream.avail_in = 0;
    size_t io;
    do {
        stream.next_out = outbuf;
        stream.avail_out = CHUNK;
        
        lzma_action action;
        if (stream.avail_in == 0) {
            io = fread(inbuf, 1, CHUNK, stdin);
            if (ferror(stdin) || (!feof(stdin) && CHUNK != io)) {
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
    
        size_t write_bytes = CHUNK - stream.avail_out;
        io = fwrite(outbuf, 1, write_bytes, stdout);
        if (io != write_bytes) {
            fprintf(stderr, "Write error\n");
            exit(1);
        }
    } while (LZMA_STREAM_END != err);
    
    return 0;
}
