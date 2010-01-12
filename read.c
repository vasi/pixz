#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lzma.h>

#include <libkern/OSByteOrder.h>


#pragma mark DEFINES

#define CHUNKSIZE 4096
#define MEMLIMIT (64L * 1024 * 1024 * 1024) // crazy high


#pragma mark TYPES

struct file_index_t {
    char *name;
    off_t offset;
    struct file_index_t *next;
};
typedef struct file_index_t file_index_t;

typedef struct {
    lzma_block block;
    lzma_filter filters[LZMA_FILTERS_MAX + 1];
} block_wrapper_t;


#pragma mark GLOBALS

FILE *gInFile = NULL;

lzma_stream gStream = LZMA_STREAM_INIT;
lzma_check gCheck = LZMA_CHECK_NONE;
lzma_index *gIndex = NULL;

file_index_t *gFileIndex = NULL, *gLastFile = NULL;

uint8_t *gFileIndexBuf = NULL;
size_t gFIBSize = CHUNKSIZE, gFIBPos = 0;
lzma_ret gFIBErr = LZMA_OK;
uint8_t gFIBInputBuf[CHUNKSIZE];
size_t gMoved = 0;


#pragma mark FUNCTION DECLARATIONS

void die(const char *fmt, ...);

void decode_index(void);
void *decode_block_start(off_t block_seek);

void dump_file_index(void);
void free_file_index(void);

void read_file_index(void);
char *read_file_index_name(void);
void read_file_index_make_space(void);
void read_file_index_data(void);

void extract_file(const char *target);
void extract_block(off_t block_seek, off_t skip, off_t size);


#pragma mark FUNCTION DEFINITIONS

int main(int argc, char **argv) {
    if (argc != 3)
        die("Need two arguments");
    if (!(gInFile = fopen(argv[1], "r")))
        die("Can't open input file");
    char *target = argv[2];
    
    decode_index();
    read_file_index();
    
    extract_file(target);
    
    free_file_index();
    lzma_index_end(gIndex, NULL);
    return 0;
}

void die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(args);
    exit(1);
}

void decode_index(void) {
    if (fseek(gInFile, -LZMA_STREAM_HEADER_SIZE, SEEK_END) == -1)
        die("Error seeking to stream footer");
    uint8_t hdrbuf[LZMA_STREAM_HEADER_SIZE];
    if (fread(hdrbuf, LZMA_STREAM_HEADER_SIZE, 1, gInFile) != 1)
        die("Error reading stream footer");
    lzma_stream_flags flags;
    if (lzma_stream_footer_decode(&flags, hdrbuf) != LZMA_OK)
        die("Error decoding stream footer");
    
    gCheck = flags.check;
    size_t index_seek = -LZMA_STREAM_HEADER_SIZE - flags.backward_size;
    if (fseek(gInFile, index_seek, SEEK_CUR) == -1)
        die("Error seeking to index");
    if (lzma_index_decoder(&gStream, &gIndex, MEMLIMIT) != LZMA_OK)
        die("Error creating index decoder");
    
    uint8_t ibuf[CHUNKSIZE];
    gStream.avail_in = 0;
    lzma_ret err = LZMA_OK;
    while (err != LZMA_STREAM_END) {
        if (gStream.avail_in == 0) {
            gStream.avail_in = fread(ibuf, 1, CHUNKSIZE, gInFile);
            if (ferror(gInFile))
                die("Error reading index");
            gStream.next_in = ibuf;
        }
        
        err = lzma_code(&gStream, LZMA_RUN);
        if (err != LZMA_OK && err != LZMA_STREAM_END)
            die("Error decoding index");
    }
}

void *decode_block_start(off_t block_seek) {
    if (fseeko(gInFile, block_seek, SEEK_SET) == -1)
        die("Error seeking to block");
    
    block_wrapper_t *bw = malloc(sizeof(block_wrapper_t));
    bw->block = (lzma_block){ .check = gCheck, .filters = bw->filters };
    
    int b = fgetc(gInFile);
    if (b == EOF || b == 0)
        die("Error reading block size");
    bw->block.header_size = lzma_block_header_size_decode(b);
    uint8_t hdrbuf[bw->block.header_size];
    hdrbuf[0] = (uint8_t)b;
    if (fread(hdrbuf + 1, bw->block.header_size - 1, 1, gInFile) != 1)
        die("Error reading block header");
    if (lzma_block_header_decode(&bw->block, NULL, hdrbuf) != LZMA_OK)
        die("Error decoding file index block header");
    
    if (lzma_block_decoder(&gStream, &bw->block) != LZMA_OK)
        die("Error initializing file index stream");
    
    return bw;
}

void read_file_index(void) {    
    // find the last block
    lzma_vli loc = lzma_index_uncompressed_size(gIndex) - 1;
    lzma_index_record rec;
    if (lzma_index_locate(gIndex, &rec, loc))
        die("Can't locate file index block");
    void *bdata = decode_block_start(rec.stream_offset);
    
    gFileIndexBuf = malloc(gFIBSize);
    gStream.avail_out = gFIBSize;
    gStream.avail_in = 0;
    while (true) {
        char *name = read_file_index_name();
        if (!name)
            break;
        
        file_index_t *f = malloc(sizeof(file_index_t));
        f->name = strlen(name) ? strdup(name) : NULL;
        f->offset = OSReadLittleInt64(gFileIndexBuf, gFIBPos);
        gFIBPos += sizeof(uint64_t);
        
        if (gLastFile) {
            gLastFile->next = f;
        } else {
            gFileIndex = f;
        }
        gLastFile = f;
    }
    free(gFileIndexBuf);
    lzma_end(&gStream);
    free(bdata);
}

char *read_file_index_name(void) {
    while (true) {
        // find a nul that ends a name
        uint8_t *eos, *haystack = gFileIndexBuf + gFIBPos;
        ssize_t len = gFIBSize - gStream.avail_out - gFIBPos - sizeof(uint64_t);
        if (len > 0 && (eos = memchr(haystack, '\0', len))) { // found it
            gFIBPos += eos - haystack + 1;
            return (char*)haystack;
        } else if (gFIBErr == LZMA_STREAM_END) { // nothing left
            return NULL;
        } else { // need more data
            if (gStream.avail_out == 0)
                read_file_index_make_space();
            read_file_index_data();            
        }
    }
}

void read_file_index_make_space(void) {
    bool expand = (gFIBPos == 0);
    if (gFIBPos != 0) { // clear more space
        size_t move = gFIBSize - gStream.avail_out - gFIBPos;        
        memmove(gFileIndexBuf, gFileIndexBuf + gFIBPos, move);
        gMoved += move;
        gStream.avail_out += gFIBPos;
        gFIBPos = 0;
    }
    // Try to reduce number of moves by expanding proactively
    if (expand || gMoved >= gFIBSize) { // malloc more space
        gStream.avail_out += gFIBSize;
        gFIBSize *= 2;
        gFileIndexBuf = realloc(gFileIndexBuf, gFIBSize);
    }
}

void read_file_index_data(void) {
    gStream.next_out = gFileIndexBuf + gFIBSize - gStream.avail_out;
    while (gFIBErr != LZMA_STREAM_END && gStream.avail_out) {
        if (gStream.avail_in == 0) {
            // It's ok to read past the end of the block, we'll still
            // get LZMA_STREAM_END at the right place
            gStream.avail_in = fread(gFIBInputBuf, 1, CHUNKSIZE, gInFile);
            if (ferror(gInFile))
                die("Error reading file index data");
            gStream.next_in = gFIBInputBuf;
        }
        
        gFIBErr = lzma_code(&gStream, LZMA_RUN);
        if (gFIBErr != LZMA_OK && gFIBErr != LZMA_STREAM_END)
            die("Error decoding file index data");
    }
}

void dump_file_index(void) {
    for (file_index_t *f = gFileIndex; f != NULL; f = f->next) {
        printf("%10llx %s\n", f->offset, f->name ? f->name : "");
    }    
}

void free_file_index(void) {
    for (file_index_t *f = gFileIndex; f != NULL; ) {
        file_index_t *next = f->next;
        free(f->name);
        free(f);
        f = next;
    }
    gFileIndex = gLastFile = NULL;
}

void extract_file(const char *target) {
    // find it in the index
    file_index_t *f;
    for (f = gFileIndex; f != NULL; f = f->next) {
        if (f->name && strcmp(f->name, target) == 0)
            break;
    }
    if (!f)
        die("Can't find target file");
    off_t fstart = f->offset, fsize = f->next->offset - fstart;
    
    // extract the data
    lzma_index_record rec;
    lzma_index_rewind(gIndex);
    while (fsize && !lzma_index_read(gIndex, &rec)) {
        off_t bstart = rec.uncompressed_offset,
            bsize = rec.uncompressed_size;
        if (fstart > bstart + bsize)
            continue;
        
        off_t dstart = fstart > bstart ? fstart - bstart : 0;
        bsize -= dstart;
        off_t dsize = fsize > bsize ? bsize : fsize;
        fsize -= dsize;
        
        extract_block(rec.stream_offset, dstart, dsize);
    }
    if (fsize)
        die("Block with file contents missing");
}

void extract_block(off_t block_seek, off_t skip, off_t size) {    
    void *bdata = decode_block_start(block_seek);
    
    uint8_t ibuf[CHUNKSIZE], obuf[CHUNKSIZE];
    gStream.avail_in = 0;
    lzma_ret err = LZMA_OK;
    while (size && err != LZMA_STREAM_END) {
        gStream.next_out = obuf;
        gStream.avail_out = CHUNKSIZE;
        
        if (gStream.avail_in == 0) {
            gStream.avail_in = fread(ibuf, 1, CHUNKSIZE, gInFile);
            if (ferror(gInFile))
                die("Error reading block data");
            gStream.next_in = ibuf;
        }
        
        err = lzma_code(&gStream, LZMA_RUN);
        if (err != LZMA_OK && err != LZMA_STREAM_END)
            die("Error decoding block");
        
        // do we want to write?
        uint8_t *start = obuf;
        size_t out = gStream.next_out - obuf;
        if (out <= skip) {
            skip -= out;
            continue;
        }
        
        // what do we want to write?
        start += skip;
        out -= skip;
        skip = 0;
        if (out > size)
            out = size;
        
        if (fwrite(start, out, 1, stdout) != 1)
            die("Error writing output");
        size -= out;
    }
    if (size)
        die("Block data missing");
    
    lzma_end(&gStream);
    free(bdata);
}
