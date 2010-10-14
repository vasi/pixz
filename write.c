#include "pixz.h"

#include <archive.h>
#include <archive_entry.h>


#pragma mark TYPES

typedef struct io_block_t io_block_t;
struct io_block_t {
    lzma_block block;
    uint8_t *input, *output;
    size_t insize, outsize;
};


#pragma mark GLOBALS

static bool gTar = true;

static size_t gBlockInSize = 0, gBlockOutSize = 0;

static off_t gMultiHeaderStart = 0;
static bool gMultiHeader = false;
static off_t gTotalRead = 0;

static pipeline_item_t *gReadItem = NULL;
static io_block_t *gReadBlock = NULL;
static size_t gReadItemCount = 0;

static lzma_filter gFilters[LZMA_FILTERS_MAX + 1];

static uint8_t gFileIndexBuf[CHUNKSIZE];
static size_t gFileIndexBufPos = 0;


#pragma mark FUNCTION DECLARATIONS

static void read_thread();
static void encode_thread(size_t thnum);
static void *block_create();
static void block_free(void *data);

static void add_file(off_t offset, const char *name);

static archive_read_callback tar_read;
static archive_open_callback tar_ok;
static archive_close_callback tar_ok;

static void block_init(lzma_block *block);
static void stream_edge(lzma_vli backward_size);
static void write_block(pipeline_item_t *pi);
static void encode_index(void);

static void write_file_index(void);
static void write_file_index_bytes(size_t size, uint8_t *buf);
static void write_file_index_buf(lzma_action action);


#pragma mark FUNCTION DEFINITIONS

void pixz_write(bool tar, uint32_t level) {
    gTar = tar;
    
    // xz options
    lzma_options_lzma lzma_opts;
    if (lzma_lzma_preset(&lzma_opts, level))
        die("Error setting lzma options");
    gFilters[0] = (lzma_filter){ .id = LZMA_FILTER_LZMA2,
            .options = &lzma_opts };
    gFilters[1] = (lzma_filter){ .id = LZMA_VLI_UNKNOWN, .options = NULL };
    
    gBlockInSize = lzma_opts.dict_size * 1.0;
    gBlockOutSize = lzma_block_buffer_bound(gBlockInSize);
    
    pipeline_create(block_create, block_free, read_thread, encode_thread);
    debug("writer: start");
    
    // pre-block setup: header, index
    if (!(gIndex = lzma_index_init(NULL)))
        die("Error creating index");
    stream_edge(LZMA_VLI_UNKNOWN);
    
    // write blocks
    while (true) {
        pipeline_item_t *pi = pipeline_merged();
        if (!pi)
            break;
        
        debug("writer: received %zu", pi->seq);
        write_block(pi);
        queue_push(gPipelineStartQ, PIPELINE_ITEM, pi);
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
    
    debug("writer: cleaning up reader");
    pipeline_destroy();
    
    debug("exit");
}


#pragma mark READING

static void read_thread() {
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
    if (gReadItem) {
        // if this block had only one read, and it was EOF, it's waste
        debug("reader: handling last block %zu", gReadItemCount);
        if (gReadBlock->insize)
            pipeline_split(gReadItem);
        else
            queue_push(gPipelineStartQ, PIPELINE_ITEM, gReadItem);
        gReadItem = NULL;
    }
    
    // stop the other threads
    debug("reader: cleaning up encoders");
    pipeline_stop();
    debug("reader: end");
}

static ssize_t tar_read(struct archive *ar, void *ref, const void **bufp) {
    if (!gReadItem) {
        queue_pop(gPipelineStartQ, (void**)&gReadItem);
        gReadBlock = (io_block_t*)(gReadItem->data);
        gReadBlock->insize = 0;
        debug("reader: reading %zu", gReadItemCount);
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
        debug("reader: sending %zu", gReadItemCount);
        pipeline_split(gReadItem);
        ++gReadItemCount;
        gReadItem = NULL;
    }
    
    return rd;
}

static int tar_ok(struct archive *ar, void *ref) {
    return ARCHIVE_OK;
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

static void block_free(void *data) {
    io_block_t *ib = (io_block_t*)data;
    free(ib->input);
    free(ib->output);
    free(ib);
}

static void *block_create() {
    io_block_t *ib = malloc(sizeof(io_block_t));
    ib->input = malloc(gBlockInSize);
    ib->output = malloc(gBlockOutSize);
    return ib;
}


#pragma mark ENCODING

static void encode_thread(size_t thnum) {
    lzma_stream stream = LZMA_STREAM_INIT;    
    while (true) {
        pipeline_item_t *pi;
        int msg = queue_pop(gPipelineSplitQ, (void**)&pi);
        if (msg == PIPELINE_STOP)
            break;
        
        debug("encoder %zu: received %zu", thnum, pi->seq);
        io_block_t *ib = (io_block_t*)(pi->data);
        
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
        
        debug("encoder %zu: sending %zu", thnum, pi->seq);
        queue_push(gPipelineMergeQ, PIPELINE_ITEM, pi);
    }
    
    lzma_end(&stream);
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

static void write_block(pipeline_item_t *pi) {
    debug("writer: writing %zu", pi->seq);
    io_block_t *ib = (io_block_t*)(pi->data);
    
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

    debug("writer: writing %zu complete", pi->seq);
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
