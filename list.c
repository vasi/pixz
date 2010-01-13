#include <lzma.h>

#include <stdio.h>
#include <string.h>
#include <sys/errno.h>

#define CHUNKSIZE 4096
#define MEMLIMIT (32 * 1024 * 1204)

void pixzlist_listfile(char *fname, FILE *f);
lzma_index *pixzlist_index(char *fname, FILE *f);

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        char *fname = argv[i];
        FILE *f = fopen(fname, "r");
        if (f == NULL) {
            fprintf(stderr, "Can't open file '%s': %s.\n", fname, strerror(errno));
            continue;
        }
        pixzlist_listfile(fname, f);
        fclose(f);
        if (i != argc - 1)
            printf("\n");
    }
    
    return 0;
}

void pixzlist_listfile(char *fname, FILE *f) {
    lzma_index *index = pixzlist_index(fname, f);
    if (!index)
        return;
    
    printf("%s:\n", fname);
    
    lzma_index_record rec;
    while (!lzma_index_read(index, &rec)) {
        printf("%llu / %llu\n", rec.unpadded_size, rec.uncompressed_size);
    }
    
    lzma_index_end(index, NULL);
}

lzma_index *pixzlist_index(char *fname, FILE *f) {
    // Seek to footer
    if (fseek(f, -LZMA_STREAM_HEADER_SIZE, SEEK_END) == -1) {
        fprintf(stderr, "Can't seek to footer in '%s': %s.\n",
            fname, strerror(errno));
        return NULL;
    }
    
    // Read footer
    uint8_t header[LZMA_STREAM_HEADER_SIZE];
    if (fread(header, LZMA_STREAM_HEADER_SIZE, 1, f) != 1) {
        fprintf(stderr, "Can't read footer from '%s': %s.\n",
            fname, strerror(errno));
        return NULL;
    }
    
    // Decode footer
    lzma_stream_flags flags;
    lzma_ret lerr = lzma_stream_footer_decode(&flags, header);
    if (lerr != LZMA_OK) {
        if (lerr == LZMA_FORMAT_ERROR)
            fprintf(stderr, "'%s' isn't an LZMA file.\n", fname);
        else if (lerr == LZMA_DATA_ERROR)
            fprintf(stderr, "CRC mismatch in '%s' footer.\n", fname);
        else
            fprintf(stderr, "Error #%d decoding footer of '%s'.\n", lerr, fname);
        return NULL;
    }
    
    // Seek to index
    if (fseek(f, -LZMA_STREAM_HEADER_SIZE - flags.backward_size, SEEK_END) == -1) {
        fprintf(stderr, "Can't seek to index in '%s': %s.\n",
            fname, strerror(errno));
        return NULL;
    }
    
    // Create index decoder
    uint8_t chunk[CHUNKSIZE];
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_index *index = NULL;
    lerr = lzma_index_decoder(&stream, &index, MEMLIMIT);
    if (lerr != LZMA_OK) {
        fprintf(stderr, "Error #%d starting decoding index of '%s'.\n",
            lerr, fname);
        return NULL;
    }
    
    // Decode index
    while (lerr != LZMA_STREAM_END) {
        size_t rd = fread(chunk, 1, CHUNKSIZE, f);
        if (rd == 0) {
            fprintf(stderr, "Error reading index from '%s': %s.\n",
                fname, strerror(errno));
            goto index_err;
        }
        stream.next_in = chunk;
        stream.avail_in = rd;
        
        while (stream.avail_in != 0 && lerr != LZMA_STREAM_END) {
            lerr = lzma_code(&stream, LZMA_RUN);
            if (lerr != LZMA_OK && lerr != LZMA_STREAM_END) {
                fprintf(stderr, "Error #%d starting decoding index of '%s'.\n",
                    lerr, fname);
                goto index_err;
            }
        }
    }
    lzma_end(&stream);
    return index;
    
index_err:
    lzma_end(&stream);
    lzma_index_end(index, NULL);
    
    return NULL;
}
