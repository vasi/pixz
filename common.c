#include "pixz.h"

#include <stdarg.h>


#pragma mark TYPES

typedef struct {
    lzma_block block;
    lzma_filter filters[LZMA_FILTERS_MAX + 1];
} block_wrapper_t;


#pragma mark GLOBALS

FILE *gInFile = NULL;
lzma_stream gStream = LZMA_STREAM_INIT;

lzma_index *gIndex = NULL;
file_index_t *gFileIndex = NULL, *gLastFile = NULL;


static lzma_check gCheck = LZMA_CHECK_NONE;

static uint8_t *gFileIndexBuf = NULL;
static size_t gFIBSize = CHUNKSIZE, gFIBPos = 0;
static lzma_ret gFIBErr = LZMA_OK;
static uint8_t gFIBInputBuf[CHUNKSIZE];
static size_t gMoved = 0;


#pragma mark FUNCTION DECLARATIONS

static char *read_file_index_name(void);
static void read_file_index_make_space(void);
static void read_file_index_data(void);


#pragma mark FUNCTION DEFINITIONS

void die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(args);
    exit(1);
}

char *xstrdup(const char *s) {
    if (!s)
        return NULL;
    size_t len = strlen(s);
    char *r = malloc(len + 1);
    if (!r)
        return NULL;
    return memcpy(r, s, len + 1); 
}

void dump_file_index(void) {
    for (file_index_t *f = gFileIndex; f != NULL; f = f->next) {
        fprintf(stderr, "%10"PRIuMAX" %s\n", (uintmax_t)f->offset, f->name ? f->name : "");
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
        f->name = strlen(name) ? xstrdup(name) : NULL;
        f->offset = xle64dec(gFileIndexBuf + gFIBPos);
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

static char *read_file_index_name(void) {
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

static void read_file_index_make_space(void) {
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

static void read_file_index_data(void) {
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

queue_t *queue_new(void) {
    queue_t *q = malloc(sizeof(queue_t));
    q->first = q->last = NULL;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->pop_cond, NULL);
    return q;
}

void queue_free(queue_t *q) {
    for (queue_item_t *i = q->first; i; ) {
        queue_item_t *tmp = i->next;
        free(i->data);
        free(i);
        i = tmp;
    }
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->pop_cond);
    free(q);
}

void queue_push(queue_t *q, int type, void *data) {
    pthread_mutex_lock(&q->mutex);
    
    queue_item_t *i = malloc(sizeof(queue_item_t));
    i->type = type;
    i->data = data;
    i->next = NULL;
    
    if (q->last) {
        q->last->next = i;
    } else {
        q->first = i;
    }
    q->last = i;
    
    pthread_cond_signal(&q->pop_cond);
    pthread_mutex_unlock(&q->mutex);
}

int queue_pop(queue_t *q, void **datap) {
    pthread_mutex_lock(&q->mutex);
    while (!q->first)
        pthread_cond_wait(&q->pop_cond, &q->mutex);
    
    queue_item_t *i = q->first;
    q->first = i->next;
    if (!q->first)
        q->last = NULL;
    
    *datap = i->data;
    int type = i->type;
    free(i);
    
    pthread_mutex_unlock(&q->mutex);
    return type;
}
