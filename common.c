#include "pixz.h"

#include <stdarg.h>


#pragma mark UTILS

typedef struct {
    lzma_block block;
    lzma_filter filters[LZMA_FILTERS_MAX + 1];
} block_wrapper_t;

FILE *gInFile = NULL;
lzma_stream gStream = LZMA_STREAM_INIT;

lzma_check gCheck = LZMA_CHECK_NONE;


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

void *decode_block_start(off_t block_seek) {
    if (fseeko(gInFile, block_seek, SEEK_SET) == -1)
        die("Error seeking to block");
    
    // Some memory in which to keep the discovered filters safe
    block_wrapper_t *bw = malloc(sizeof(block_wrapper_t));
    bw->block = (lzma_block){ .check = gCheck, .filters = bw->filters,
	 	.version = 0 };
    
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

bool is_multi_header(const char *name) {
    size_t i = strlen(name);
    while (i != 0 && name[i - 1] != '/')
        --i;
    
    return strncmp(name + i, "._", 2) == 0;
}


#pragma mark INDEX

lzma_index *gIndex = NULL;
file_index_t *gFileIndex = NULL, *gLastFile = NULL;

static uint8_t *gFileIndexBuf = NULL;
static size_t gFIBSize = CHUNKSIZE, gFIBPos = 0;
static lzma_ret gFIBErr = LZMA_OK;
static uint8_t gFIBInputBuf[CHUNKSIZE];
static size_t gMoved = 0;

static char *read_file_index_name(void);
static void read_file_index_make_space(void);
static void read_file_index_data(void);


void dump_file_index(FILE *out, bool verbose) {
    for (file_index_t *f = gFileIndex; f != NULL; f = f->next) {
        if (verbose) {
            fprintf(out, "%10"PRIuMAX" %s\n", (uintmax_t)f->offset,
                f->name ? f->name : "");
        } else {
            if (f->name)
                fprintf(out, "%s\n", f->name);
        }
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

lzma_vli find_file_index(void **bdatap) {
    if (!gIndex)
        decode_index();
        
    // find the last block
    lzma_index_iter iter;
	lzma_index_iter_init(&iter, gIndex);
    lzma_vli loc = lzma_index_uncompressed_size(gIndex) - 1;
    if (lzma_index_iter_locate(&iter, loc))
        die("Can't locate file index block");
    void *bdata = decode_block_start(iter.block.compressed_file_offset);
    
    gFileIndexBuf = malloc(gFIBSize);
    gStream.avail_out = gFIBSize;
    gStream.avail_in = 0;
    
    // Check if this is really an index
    read_file_index_data();
    lzma_vli ret = iter.block.compressed_file_offset;
    if (xle64dec(gFileIndexBuf + gFIBPos) != PIXZ_INDEX_MAGIC)
        ret = 0;
    gFIBPos += sizeof(uint64_t);
    
    if (bdatap && ret) {
        *bdatap = bdata;
    } else {
        // Just looking, don't keep things around
        if (bdatap)
            *bdatap = NULL;
        free(bdata);
        free(gFileIndexBuf);
        gLastFile = gFileIndex = NULL;
        lzma_end(&gStream);
    }
    return ret; 
}  

lzma_vli read_file_index(lzma_vli offset) {
    void *bdata = NULL;
    if (!offset)
        offset = find_file_index(&bdata);
    if (!offset)
        return 0;
    
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
    
    return offset;
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


#pragma mark QUEUE

queue_t *queue_new(queue_free_t freer) {
    queue_t *q = malloc(sizeof(queue_t));
    q->first = q->last = NULL;
    q->freer = freer;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->pop_cond, NULL);
    return q;
}

void queue_free(queue_t *q) {
    for (queue_item_t *i = q->first; i; ) {
        queue_item_t *tmp = i->next;
        if (q->freer)
            q->freer(i->type, i->data);
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


#pragma mark PIPELINE

queue_t *gPipelineStartQ = NULL,
    *gPipelineSplitQ = NULL,
    *gPipelineMergeQ = NULL;

pipeline_data_free_t gPLFreer = NULL;
pipeline_split_t gPLSplit = NULL;
pipeline_process_t gPLProcess = NULL;

size_t gPLProcessCount = 0;
pthread_t *gPLProcessThreads = NULL;
pthread_t gPLSplitThread;

ssize_t gPLSplitSeq = 0;
ssize_t gPLMergeSeq = 0;
pipeline_item_t *gPLMergedItems = NULL;

static void pipeline_qfree(int type, void *p);
static void *pipeline_thread_split(void *);
static void *pipeline_thread_process(void *arg);

void pipeline_create(
        pipeline_data_create_t create,
        pipeline_data_free_t destroy,
        pipeline_split_t split,
        pipeline_process_t process) {
    gPLFreer = destroy;
    gPLSplit = split;
    gPLProcess = process;
    
    gPipelineStartQ = queue_new(pipeline_qfree);
    gPipelineSplitQ = queue_new(pipeline_qfree);
    gPipelineMergeQ = queue_new(pipeline_qfree);
    
    gPLSplitSeq = 0;
    gPLMergeSeq = 0;
    gPLMergedItems = NULL;
    
    gPLProcessCount = num_threads();
    gPLProcessThreads = malloc(gPLProcessCount * sizeof(pthread_t));
    for (size_t i = 0; i < (int)(gPLProcessCount * 2 + 3); ++i) {
        // create blocks, including a margin of error
        pipeline_item_t *item = malloc(sizeof(pipeline_item_t));
        item->data = create();
        // seq and next are garbage
        queue_push(gPipelineStartQ, PIPELINE_ITEM, item);
    }
    for (size_t i = 0; i < gPLProcessCount; ++i) {
        if (pthread_create(&gPLProcessThreads[i], NULL,
                &pipeline_thread_process, (void*)(uintptr_t)i))
            die("Error creating encode thread");
    }
    if (pthread_create(&gPLSplitThread, NULL, &pipeline_thread_split, NULL))
        die("Error creating read thread");
}

static void pipeline_qfree(int type, void *p) {
    switch (type) {
        case PIPELINE_ITEM: {
            pipeline_item_t *item = (pipeline_item_t*)p;
            gPLFreer(item->data);
            free(item);
            break;
        }
        case PIPELINE_STOP:
            break;
        default:
            die("Unknown msg type %d", type);
    }
}

static void *pipeline_thread_split(void *ignore) {
    gPLSplit();
    return NULL;
}

static void *pipeline_thread_process(void *arg) {
    size_t thnum = (uintptr_t)arg;
    gPLProcess(thnum);
    return NULL;
}

void pipeline_stop(void) {
    // ask the other threads to stop
    for (size_t i = 0; i < gPLProcessCount; ++i)
        queue_push(gPipelineSplitQ, PIPELINE_STOP, NULL);
    for (size_t i = 0; i < gPLProcessCount; ++i) {
        if (pthread_join(gPLProcessThreads[i], NULL))
            die("Error joining processing thread");
    }
    queue_push(gPipelineMergeQ, PIPELINE_STOP, NULL);
}

void pipeline_destroy(void) {
    if (pthread_join(gPLSplitThread, NULL))
        die("Error joining splitter thread");
    
    queue_free(gPipelineStartQ);
    queue_free(gPipelineSplitQ);
    queue_free(gPipelineMergeQ);
    free(gPLProcessThreads);
}

void pipeline_split(pipeline_item_t *item) {
    item->seq = gPLSplitSeq++;
    item->next = NULL;
    queue_push(gPipelineSplitQ, PIPELINE_ITEM, item);
}

pipeline_item_t *pipeline_merged() {
    pipeline_item_t *item;
    while (!gPLMergedItems || gPLMergedItems->seq != gPLMergeSeq) {
        // We don't have the next item, wait for a new one
        pipeline_tag_t tag = queue_pop(gPipelineMergeQ, (void**)&item);
        if (tag == PIPELINE_STOP)
            return NULL; // Done processing items
        
        // Insert the item into the queue
        pipeline_item_t **prev = &gPLMergedItems;
        while (*prev && (*prev)->seq < item->seq) {
            prev = &(*prev)->next;
        }
        item->next = *prev;
        *prev = item;
    }
    
    // Got the next item
    item = gPLMergedItems;
    gPLMergedItems = item->next;
    ++gPLMergeSeq;
    return item;
}
