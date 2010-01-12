#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <archive.h>
#include <archive_entry.h>
#include <lzma.h>

#include <libkern/OSByteOrder.h>


#pragma mark DEFINES

#define CHUNKSIZE 4096
#define BLOCKSIZE (1024 * 1024)

#define CHECK LZMA_CHECK_CRC32


#pragma mark TYPES

struct file_index_t {
    char *name;
    off_t offset;
    struct file_index_t *next;
};
typedef struct file_index_t file_index_t;


#pragma mark GLOBALS

FILE *gInFile = NULL, *gOutFile = NULL;
off_t gTotalRead = 0;

uint8_t gBlockBuf[BLOCKSIZE];
size_t gBlockSize = 0;
lzma_filter gFilters[LZMA_FILTERS_MAX + 1];
lzma_stream gStream = LZMA_STREAM_INIT;
lzma_index *gIndex = NULL;

file_index_t *gFileIndex = NULL;
file_index_t *gLastFile = NULL;
off_t gMultiHeaderStart = 0;
bool gMultiHeader = false;

uint8_t gFileIndexBuf[CHUNKSIZE];
size_t gFileIndexBufPos = 0;


#pragma mark FUNCTION DECLARATIONS

void die(const char *fmt, ...);

void stream_edge(lzma_vli backward_size);
void write_block(void);
void write_block_header(lzma_block *block);
void encode_index(void);

bool is_multi_header(const char *name);
void add_file(off_t offset, const char *name);
void dump_file_index(void);
void free_file_index(void);

void write_file_index(void);
void write_file_index_bytes(size_t size, uint8_t *buf);
void write_file_index_buf(lzma_action action);

archive_read_callback tar_read;
archive_open_callback tar_ok;
archive_close_callback tar_ok;


#pragma mark FUNCTION DEFINITIONS

int main(int argc, char **argv) {
    if (argc != 3)
        die("Need two arguments");
    if (!(gInFile = fopen(argv[1], "r")))
        die("Can't open input file");
    if (!(gOutFile = fopen(argv[2], "w")))
        die("Can't open output file");
    
    // xz options
    lzma_options_lzma lzma_opts;
    if (lzma_lzma_preset(&lzma_opts, LZMA_PRESET_DEFAULT))
        die("Error setting lzma options");
    gFilters[0] = (lzma_filter){ .id = LZMA_FILTER_LZMA2,
            .options = &lzma_opts };
    gFilters[1] = (lzma_filter){ .id = LZMA_VLI_UNKNOWN, .options = NULL };
    
    // xz setup (index, header)
    if (!(gIndex = lzma_index_init(NULL, NULL)))
        die("Error creating index");
    stream_edge(LZMA_VLI_UNKNOWN);
    
    // read archive
    struct archive *ar = archive_read_new();
    archive_read_support_compression_none(ar);
    archive_read_support_format_tar(ar);
    archive_read_open(ar, NULL, tar_ok, tar_read, tar_ok);
    struct archive_entry *entry;
    while (true) {
        int aerr = archive_read_next_header(ar, &entry);
        if (aerr == ARCHIVE_EOF) {
            // TODO
            break;
        } else if (aerr != ARCHIVE_OK && aerr != ARCHIVE_WARN) {
            // Some charset translations warn spuriously
            fprintf(stderr, "%s\n", archive_error_string(ar));
            die("Error reading archive entry");
        }
        
        add_file(archive_read_header_position(ar),
            archive_entry_pathname(entry));
    }    
    archive_read_finish(ar);
    fclose(gInFile);
    
    write_block(); // write last block, if necessary
    
    add_file(gTotalRead, NULL);
    write_file_index();
    
    // xz cleanup (index, footer)
    encode_index();
    stream_edge(lzma_index_size(gIndex));
    lzma_index_end(gIndex, NULL);
    lzma_end(&gStream);
    fclose(gOutFile);
    
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

ssize_t tar_read(struct archive *ar, void *ref, const void **bufp) {
    if (gBlockSize == BLOCKSIZE)
        write_block();
    
    uint8_t *buf = gBlockBuf + gBlockSize;
    size_t rd = fread(buf, 1, CHUNKSIZE, gInFile);
    if (rd == 0 && feof(gInFile))
        die("Error reading input file");
    gBlockSize += rd;
    gTotalRead += rd;
    *bufp = buf;
    return rd;
}

int tar_ok(struct archive *ar, void *ref) {
    return ARCHIVE_OK;
}

void stream_edge(lzma_vli backward_size) {
    lzma_stream_flags flags = { .version = 0, .check = CHECK,
        .backward_size = backward_size };
    uint8_t buf[LZMA_STREAM_HEADER_SIZE];
    
    lzma_ret (*encoder)(const lzma_stream_flags *flags, uint8_t *buf);
    encoder = backward_size == LZMA_VLI_UNKNOWN
        ? &lzma_stream_header_encode
        : &lzma_stream_footer_encode;
    if ((*encoder)(&flags, buf) != LZMA_OK)
        die("Error encoding stream edge");
    
    if (fwrite(buf, LZMA_STREAM_HEADER_SIZE, 1, gOutFile) != 1)
        die("Error writing stream edge");
}

void write_block_header(lzma_block *block) {
    block->version = 0;
    block->check = CHECK;
    block->filters = gFilters;
    block->compressed_size = block->uncompressed_size = LZMA_VLI_UNKNOWN;
    
    if (lzma_block_header_size(block) != LZMA_OK)
        die("Error getting block header size");
    
    uint8_t buf[block->header_size];
    if (lzma_block_header_encode(block, buf) != LZMA_OK)
        die("Error encoding block header");
    if (fwrite(buf, block->header_size, 1, gOutFile) != 1)
        die("Error writing block header");
}

void write_block(void) {
    if (gBlockSize == 0)
        return;
    
    lzma_block block;
    write_block_header(&block);
    
    if (lzma_block_encoder(&gStream, &block) != LZMA_OK)
        die("Error creating block encoder");
    gStream.next_in = gBlockBuf;
    gStream.avail_in = gBlockSize;
    uint8_t obuf[CHUNKSIZE];
    lzma_ret err = LZMA_OK;
    while (err != LZMA_STREAM_END) {
        gStream.next_out = obuf;
        gStream.avail_out = CHUNKSIZE;
        
        err = lzma_code(&gStream, gStream.avail_in ? LZMA_RUN : LZMA_FINISH);
        if (err != LZMA_OK && err != LZMA_STREAM_END)
            die("Error encoding block");
        
        if (gStream.avail_out != CHUNKSIZE) {
            if (fwrite(obuf, CHUNKSIZE - gStream.avail_out, 1, gOutFile) != 1)
                die("Error writing block data");
        }
    }
    
    if (lzma_index_append(gIndex, NULL, lzma_block_unpadded_size(&block),
            block.uncompressed_size) != LZMA_OK)
        die("Error adding to index");
    
    gBlockSize = 0;
}

void encode_index(void) {
    if (lzma_index_encoder(&gStream, gIndex) != LZMA_OK)
        die("Error creating index encoder");
    uint8_t obuf[CHUNKSIZE];
    lzma_ret err = LZMA_OK;
    while (err != LZMA_STREAM_END) {
        gStream.next_out = obuf;
        gStream.avail_out = CHUNKSIZE;
        err = lzma_code(&gStream, LZMA_RUN);
        if (err != LZMA_OK && err != LZMA_STREAM_END)
            die("Error encoding index");
        if (gStream.avail_out != CHUNKSIZE) {
            if (fwrite(obuf, CHUNKSIZE - gStream.avail_out, 1, gOutFile) != 1)
                die("Error writing index data");
        }
    }
}

void add_file(off_t offset, const char *name) {
    if (name && is_multi_header(name)) {
        if (!gMultiHeader)
            gMultiHeaderStart = offset;
        gMultiHeader = true;
        return;
    }
    
    file_index_t *f = malloc(sizeof(file_index_t));
    f->offset = gMultiHeader ? gMultiHeaderStart : offset;
    gMultiHeader = false;
    f->name = name ? strdup(name) : NULL;
    f->next = NULL;
    
    if (gLastFile) {
        gLastFile->next = f;
    } else { // new index
        gFileIndex = f;
    }
    gLastFile = f;
}

void dump_file_index(void) {
    for (file_index_t *f = gFileIndex; f != NULL; f = f->next) {
        printf("%s\n", f->name ? f->name : "");
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

bool is_multi_header(const char *name) {
    size_t i = strlen(name);
    while (i != 0 && name[i - 1] != '/')
        --i;
    
    return strncmp(name + i, "._", 2) == 0;
}

void write_file_index(void) {
    lzma_block block;
    write_block_header(&block);
    if (lzma_block_encoder(&gStream, &block) != LZMA_OK)
        die("Error creating file index encoder");
    
    uint8_t offbuf[sizeof(uint64_t)]; 
    for (file_index_t *f = gFileIndex; f != NULL; f = f->next) {
        char *name = f->name ? f->name : "";
        printf("%s\n", name);
        size_t len = strlen(name);
        write_file_index_bytes(len + 1, (uint8_t*)name);
        OSWriteLittleInt64(offbuf, 0, f->offset);
        write_file_index_bytes(sizeof(offbuf), offbuf);
    }
    write_file_index_buf(LZMA_FINISH);

    if (lzma_index_append(gIndex, NULL, lzma_block_unpadded_size(&block),
            block.uncompressed_size) != LZMA_OK)
        die("Error adding file-index to index");
}

void write_file_index_bytes(size_t size, uint8_t *buf) {
    size_t bufpos = 0;
    while (bufpos < size) {
        size_t len = size - bufpos;
        size_t space = CHUNKSIZE - gFileIndexBufPos;
        if (len > space)
            len = space;
        memcpy(gFileIndexBuf + gFileIndexBufPos, buf + bufpos, len);
        gFileIndexBufPos += len;
        bufpos += len;
        
        if (gFileIndexBufPos == CHUNKSIZE) {
            write_file_index_buf(LZMA_RUN);
            gFileIndexBufPos = 0;
        }
    }
}

void write_file_index_buf(lzma_action action) {
    printf("%ld\n", gFileIndexBufPos);
    uint8_t obuf[CHUNKSIZE];
    gStream.avail_in = gFileIndexBufPos;
    gStream.next_in = gFileIndexBuf;
    lzma_ret err = LZMA_OK;
    while (err != LZMA_STREAM_END && (action == LZMA_FINISH || gStream.avail_in)) {
        gStream.avail_out = CHUNKSIZE;
        gStream.next_out = obuf;
        err = lzma_code(&gStream, action);
        if (err != LZMA_OK && err != LZMA_STREAM_END)
            die("Error encoding file index");
        if (gStream.avail_out != CHUNKSIZE) {
            if (fwrite(obuf, CHUNKSIZE - gStream.avail_out, 1, gOutFile) != 1)
                die("Error writing file index");
        }
    }
    
    gFileIndexBufPos = 0;
}
