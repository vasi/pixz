#include "pixz.h"

#include <archive.h>
#include <archive_entry.h>

#include <getopt.h>

#pragma mark TYPES

typedef enum {
    MSG_BLOCK,
    MSG_STOP,
} msg_type_t;

typedef struct io_block_t io_block_t;
struct io_block_t {
    size_t seq;
    io_block_t *next;
    
    lzma_block block;
    uint8_t *input, *output;
    size_t insize, outsize;
};


#pragma mark GLOBALS

#define DEBUG 0

static bool gTar = true;
static uint32_t gPreset = LZMA_PRESET_DEFAULT;

static size_t gNumEncodeThreads = 0;
static pthread_t *gEncodeThreads = NULL;
static pthread_t gReadThread;
static queue_t *gReadQ, *gEncodeQ, *gWriteQ;
static size_t gBlockInSize = 0, gBlockOutSize = 0;

static off_t gMultiHeaderStart = 0;
static bool gMultiHeader = false;
static off_t gTotalRead = 0;
static size_t gBlockNum = 0;
static io_block_t *gReadBlock = NULL;

static lzma_filter gFilters[LZMA_FILTERS_MAX + 1];

static FILE *gOutFile = NULL;
static uint8_t gFileIndexBuf[CHUNKSIZE];
static size_t gFileIndexBufPos = 0;


#pragma mark FUNCTION DECLARATIONS

#if DEBUG
    #define debug(str, ...) fprintf(stderr, str "\n", ##__VA_ARGS__)
#else
    #define debug(...)
#endif

static void *read_thread(void *data);
static void *encode_thread(void *data);
static void block_queue_free(int type, void *p);

static bool is_multi_header(const char *name);
static void add_file(off_t offset, const char *name);

static archive_read_callback tar_read;
static archive_open_callback tar_ok;
static archive_close_callback tar_ok;

static void block_init(lzma_block *block);
static void stream_edge(lzma_vli backward_size);
static void write_blocks(size_t *seq, io_block_t **ibs, io_block_t *ib);
static void write_block(io_block_t *ib);
static void encode_index(void);

static void write_file_index(void);
static void write_file_index_bytes(size_t size, uint8_t *buf);
static void write_file_index_buf(lzma_action action);


#pragma mark FUNCTION DEFINITIONS

int main(int argc, char **argv) {
    debug("launch");
    
    int ch;
    while ((ch = getopt(argc, argv, "t0123456789")) != -1) {
        switch (ch) {
            case 't':
                gTar = false;
                break;
            default:
                if (optopt >= '0' && optopt <= '9') {
                    gPreset = optopt - '0';
                } else {
                    die("Unknown option");
                }
        }
    }
    argc -= optind - 1;
    argv += optind - 1;
    

    if (argc != 3)
        die("Need two arguments");
    if (!(gInFile = fopen(argv[1], "r")))
        die("Can't open input file");
    if (!(gOutFile = fopen(argv[2], "w")))
        die("Can't open output file");
    
    // xz options
    lzma_options_lzma lzma_opts;
    if (lzma_lzma_preset(&lzma_opts, gPreset))
        die("Error setting lzma options");
    gFilters[0] = (lzma_filter){ .id = LZMA_FILTER_LZMA2,
            .options = &lzma_opts };
    gFilters[1] = (lzma_filter){ .id = LZMA_VLI_UNKNOWN, .options = NULL };
    
    gBlockInSize = lzma_opts.dict_size * 1.0;
    gBlockOutSize = lzma_block_buffer_bound(gBlockInSize);
    
    // thread setup
    gNumEncodeThreads = num_threads();
    gEncodeThreads = malloc(gNumEncodeThreads * sizeof(pthread_t));
    gReadQ = queue_new(block_queue_free);
    gEncodeQ = queue_new(block_queue_free);
    gWriteQ = queue_new(block_queue_free);
    for (size_t i = 0; i < (int)(gNumEncodeThreads * 2 + 4); ++i) {
        // create blocks, including a margin of error
        io_block_t *ib = malloc(sizeof(io_block_t));
        ib->input = malloc(gBlockInSize);
        ib->output = malloc(gBlockOutSize);
        queue_push(gReadQ, MSG_BLOCK, ib);
    }
    if (pthread_create(&gReadThread, NULL, &read_thread, NULL))
        die("Error creating read thread");
    for (int i = 0; i < gNumEncodeThreads; ++i) {
        if (pthread_create(&gEncodeThreads[i], NULL, &encode_thread, (void*)(uintptr_t)i))
            die("Error creating encode thread");
    }
    debug("writer: start");
    
    // pre-block setup: header, index
    if (!(gIndex = lzma_index_init(NULL)))
        die("Error creating index");
    stream_edge(LZMA_VLI_UNKNOWN);
    
    // write blocks
    size_t seq = 0;
    io_block_t *ibs = NULL;
    while (true) {
        io_block_t *ib;
        int msg = queue_pop(gWriteQ, (void**)&ib);
        if (msg == MSG_STOP)
            break;
        
        debug("writer: received %zu", ib->seq);
        write_blocks(&seq, &ibs, ib);
    }
    
    // file index
    if (gTar) {
        write_file_index();
        free_file_index();
    }
    
    // post-block cleanup: index, footer
    encode_index();
    stream_edge(lzma_index_size(gIndex));
    lzma_index_end(gIndex, NULL);
    fclose(gOutFile);
    
    // thread cleanup
    debug("writer: cleaning up reader");
    if (pthread_join(gReadThread, NULL))
        die("Error joining read thread");
    queue_free(gEncodeQ);
    queue_free(gWriteQ);
    queue_free(gReadQ);
    free(gEncodeThreads);
    
    debug("exit");
    return 0;
}


#pragma mark READING

static void *read_thread(void *data) {
    debug("reader: start");
    
    struct archive *ar = archive_read_new();
    archive_read_support_compression_none(ar);
    if (gTar)
        archive_read_support_format_tar(ar);
    archive_read_support_format_raw(ar);
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
        
        if (archive_format(ar) == ARCHIVE_FORMAT_RAW)
            gTar = false;
        if (gTar) {
            add_file(archive_read_header_position(ar),
                archive_entry_pathname(entry));
        }
    }
    archive_read_finish(ar);
    fclose(gInFile);
    if (gTar)
        add_file(gTotalRead, NULL);
    
    // write last block, if necessary
    if (gReadBlock) {
        // if this block had only one read, and it was EOF, it's waste
        debug("reader: handling last block %zu", gReadBlock->seq);
        queue_push(gReadBlock->insize ? gEncodeQ : gReadQ, MSG_BLOCK, gReadBlock);
        gReadBlock = NULL;
    }
    
    // stop the other threads
    debug("reader: cleaning up encoders");
    for (int i = 0; i < gNumEncodeThreads; ++i) {
        queue_push(gEncodeQ, MSG_STOP, NULL);
    }
    for (int i = 0; i < gNumEncodeThreads; ++i) {
        if (pthread_join(gEncodeThreads[i], NULL))
            die("Error joining encode thread");
    }
    queue_push(gWriteQ, MSG_STOP, NULL);
    
    debug("reader: end");
    return NULL;
}

static ssize_t tar_read(struct archive *ar, void *ref, const void **bufp) {
    if (!gReadBlock) {
        queue_pop(gReadQ, (void**)&gReadBlock);
        gReadBlock->insize = 0;
        gReadBlock->seq = gBlockNum++;
        debug("reader: reading %zu", gReadBlock->seq);
    }
    
    size_t space = gBlockInSize - gReadBlock->insize;
    if (space > CHUNKSIZE)
        space = CHUNKSIZE;    
    uint8_t *buf = gReadBlock->input + gReadBlock->insize;
    size_t rd = fread(buf, 1, space, gInFile);
    if (ferror(gInFile))
        die("Error reading input file");
    gReadBlock->insize += rd;
    gTotalRead += rd;
    *bufp = buf;
    
    if (gReadBlock->insize == gBlockInSize) {
        debug("reader: sending %zu", gReadBlock->seq);
        queue_push(gEncodeQ, MSG_BLOCK, gReadBlock);
        gReadBlock = NULL;
    }
    
    return rd;
}

static int tar_ok(struct archive *ar, void *ref) {
    return ARCHIVE_OK;
}

static bool is_multi_header(const char *name) {
    size_t i = strlen(name);
    while (i != 0 && name[i - 1] != '/')
        --i;
    
    return strncmp(name + i, "._", 2) == 0;
}

static void add_file(off_t offset, const char *name) {
    if (name && is_multi_header(name)) {
        if (!gMultiHeader)
            gMultiHeaderStart = offset;
        gMultiHeader = true;
        return;
    }
    
    file_index_t *f = malloc(sizeof(file_index_t));
    f->offset = gMultiHeader ? gMultiHeaderStart : offset;
    gMultiHeader = false;
    f->name = name ? xstrdup(name) : NULL;
    f->next = NULL;
    
    if (gLastFile) {
        gLastFile->next = f;
    } else { // new index
        gFileIndex = f;
    }
    gLastFile = f;
}

static void block_queue_free(int type, void *p) {
    switch (type) {
        case MSG_BLOCK: {
            io_block_t *ib = (io_block_t*)p;
            free(ib->input);
            free(ib->output);
            free(ib);
            break;
        }
        case MSG_STOP:
            break;
        default:
            die("Unknown msg type %d", type);
    }
}


#pragma mark ENCODING

static void *encode_thread(void *arg) {
    int __attribute__((unused)) thnum = (uintptr_t)arg;
    lzma_stream stream = LZMA_STREAM_INIT;
    
    while (true) {
        io_block_t *ib;
        int msg = queue_pop(gEncodeQ, (void**)&ib);
        if (msg == MSG_STOP)
            break;
        
        debug("encoder %d: received %zu", thnum, ib->seq);
        
        block_init(&ib->block);
        if (lzma_block_header_encode(&ib->block, ib->output) != LZMA_OK)
            die("Error encoding block header");
        ib->outsize = ib->block.header_size;
        
        if (lzma_block_encoder(&stream, &ib->block) != LZMA_OK)
            die("Error creating block encoder");
        stream.next_in = ib->input;
        stream.avail_in = ib->insize;
        stream.next_out = ib->output + ib->outsize;
        stream.avail_out = gBlockOutSize - ib->outsize;
        
        lzma_ret err = LZMA_OK;
        while (err != LZMA_STREAM_END) {
            err = lzma_code(&stream, LZMA_FINISH);
            if (err != LZMA_OK && err != LZMA_STREAM_END)
                die("Error encoding block");
        }
        ib->outsize = stream.next_out - ib->output;
        
        debug("encoder %d: sending %zu", thnum, ib->seq);
        queue_push(gWriteQ, MSG_BLOCK, ib);
    }
    
    lzma_end(&stream);
    return NULL;
}


#pragma mark WRITING

static void block_init(lzma_block *block) {
    block->version = 0;
    block->check = CHECK;
    block->filters = gFilters;
    block->compressed_size = block->uncompressed_size = LZMA_VLI_UNKNOWN;
    
    if (lzma_block_header_size(block) != LZMA_OK)
        die("Error getting block header size");
}

static void stream_edge(lzma_vli backward_size) {
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

static void write_block(io_block_t *ib) {
    debug("writer: writing %zu", ib->seq);

    // Does it make sense to chunk this?
    size_t written = 0;
    while (ib->outsize > written) {
        size_t size = ib->outsize - written;
        if (size > CHUNKSIZE)
            size = CHUNKSIZE;
        if (fwrite(ib->output + written, size, 1, gOutFile) != 1)
            die("Error writing block data");
        written += size;
    }
    
    if (lzma_index_append(gIndex, NULL,
            lzma_block_unpadded_size(&ib->block),
            ib->block.uncompressed_size) != LZMA_OK)
        die("Error adding to index");

    debug("writer: writing %zu complete", ib->seq);
}

static void write_blocks(size_t *seq, io_block_t **ibs, io_block_t *ib) {
    // insert it into the queue, in order
    io_block_t **prev = ibs, *post = *ibs;
    while (post && post->seq < ib->seq) {
        prev = &post->next;
        post = post->next;
    }
    ib->next = post;
    *prev = ib;
    
    // write the blocks that we can
    io_block_t *cur = *ibs;
    while (cur && cur->seq == *seq) {
        write_block(cur);
        queue_push(gReadQ, MSG_BLOCK, cur);
        ++*seq;
        cur = cur->next;
    }
    *ibs = cur;
}

static void encode_index(void) {
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
    lzma_end(&gStream);
}

static void write_file_index(void) {
    lzma_block block;
    block_init(&block);
    uint8_t hdrbuf[block.header_size];
    if (lzma_block_header_encode(&block, hdrbuf) != LZMA_OK)
        die("Error encoding file index header");
    if (fwrite(hdrbuf, block.header_size, 1, gOutFile) != 1)
        die("Error writing file index header");
    
    if (lzma_block_encoder(&gStream, &block) != LZMA_OK)
        die("Error creating file index encoder");
    
    uint8_t offbuf[sizeof(uint64_t)];
    xle64enc(offbuf, PIXZ_INDEX_MAGIC);
    write_file_index_bytes(sizeof(offbuf), offbuf);
    for (file_index_t *f = gFileIndex; f != NULL; f = f->next) {
        char *name = f->name ? f->name : "";
        size_t len = strlen(name);
        write_file_index_bytes(len + 1, (uint8_t*)name);
        xle64enc(offbuf, f->offset);
        write_file_index_bytes(sizeof(offbuf), offbuf);
    }
    write_file_index_buf(LZMA_FINISH);

    if (lzma_index_append(gIndex, NULL, lzma_block_unpadded_size(&block),
            block.uncompressed_size) != LZMA_OK)
        die("Error adding file-index to index");
    lzma_end(&gStream);
}

static void write_file_index_bytes(size_t size, uint8_t *buf) {
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

static void write_file_index_buf(lzma_action action) {
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
