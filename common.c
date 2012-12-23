#include "pixz.h"

#include <stdarg.h>
#include <math.h>


#pragma mark UTILS

FILE *gInFile = NULL;
lzma_stream gStream = LZMA_STREAM_INIT;


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

static void *decode_file_index_start(off_t block_seek, lzma_check check);
static lzma_vli find_file_index(void **bdatap);

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

typedef struct {
    lzma_block block;
    lzma_filter filters[LZMA_FILTERS_MAX + 1];
} block_wrapper_t;

static void *decode_file_index_start(off_t block_seek, lzma_check check) {
    if (fseeko(gInFile, block_seek, SEEK_SET) == -1)
        die("Error seeking to block");
    
    // Some memory in which to keep the discovered filters safe
    block_wrapper_t *bw = malloc(sizeof(block_wrapper_t));
    bw->block = (lzma_block){ .check = check, .filters = bw->filters,
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

static lzma_vli find_file_index(void **bdatap) {
    if (!gIndex)
        decode_index();
        
    // find the last block
    lzma_index_iter iter;
	lzma_index_iter_init(&iter, gIndex);
    lzma_vli loc = lzma_index_uncompressed_size(gIndex) - 1;
    if (lzma_index_iter_locate(&iter, loc))
        die("Can't locate file index block");
	if (iter.stream.number != 1)
		return 0; // Too many streams for one file index
	
    void *bdata = decode_file_index_start(iter.block.compressed_file_offset,
		iter.stream.flags->check);
    
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

lzma_vli read_file_index() {
    void *bdata = NULL;
	lzma_vli offset = find_file_index(&bdata);
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


#define BWCHUNK 512

typedef struct {
	uint8_t buf[BWCHUNK];
	off_t pos;
	size_t size;
} bw;

static uint32_t *bw_read(bw *b) {
	size_t sz = sizeof(uint32_t);
	if (b->size < sz) {
		if (b->pos < sz)
			return NULL; // EOF
		b->size = (b->pos > BWCHUNK) ? BWCHUNK : b->pos;
		b->pos -= b->size;
		if (fseeko(gInFile, b->pos, SEEK_SET) == -1)
			return NULL;
		if (fread(b->buf, b->size, 1, gInFile) != 1)
			return NULL;
	}
	
	b->size -= sz;
	return &((uint32_t*)b->buf)[b->size / sz];
}

static off_t stream_padding(bw *b, off_t pos) {
	b->pos = pos;
	b->size = 0;
	
	for (off_t pad = 0; true; pad += sizeof(uint32_t)) {
		uint32_t *i = bw_read(b);
		if (!i)
			die("Error reading stream padding");
		if (*i != 0) {
			b->size += sizeof(uint32_t);
			return pad;
		}
	}
}

static void stream_footer(bw *b, lzma_stream_flags *flags) {
	uint8_t ftr[LZMA_STREAM_HEADER_SIZE];
	for (int i = sizeof(ftr) / sizeof(uint32_t) - 1; i >= 0; --i) {
		uint32_t *p = bw_read(b);
		if (!p)
			die("Error reading stream footer");
		*((uint32_t*)ftr + i) = *p;
	}
	
    if (lzma_stream_footer_decode(flags, ftr) != LZMA_OK)
        die("Error decoding stream footer");
}

static lzma_index *next_index(off_t *pos) {
	bw b;
	off_t pad = stream_padding(&b, *pos);
	off_t eos = *pos - pad;
	
	lzma_stream_flags flags;
	stream_footer(&b, &flags);
	*pos = eos - LZMA_STREAM_HEADER_SIZE - flags.backward_size;
    if (fseeko(gInFile, *pos, SEEK_SET) == -1)
        die("Error seeking to index");
	
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_index *index;
    if (lzma_index_decoder(&strm, &index, MEMLIMIT) != LZMA_OK)
        die("Error creating index decoder");
    
    uint8_t ibuf[CHUNKSIZE];
    strm.avail_in = 0;
    lzma_ret err = LZMA_OK;
    while (err != LZMA_STREAM_END) {
        if (strm.avail_in == 0) {
            strm.avail_in = fread(ibuf, 1, CHUNKSIZE, gInFile);
            if (ferror(gInFile))
                die("Error reading index");
            strm.next_in = ibuf;
        }
        
        err = lzma_code(&strm, LZMA_RUN);
        if (err != LZMA_OK && err != LZMA_STREAM_END)
            die("Error decoding index");
    }
	
	*pos = eos - lzma_index_stream_size(index);
	if (fseeko(gInFile, *pos, SEEK_SET) == -1)
		die("Error seeking to beginning of stream");
	
	
	if (lzma_index_stream_flags(index, &flags) != LZMA_OK)
		die("Error setting stream flags");
	if (lzma_index_stream_padding(index, pad) != LZMA_OK)
		die("Error setting stream padding");
	return index;
}

bool decode_index(void) {
    if (fseeko(gInFile, 0, SEEK_END) == -1)
		return false; // not seekable
	off_t pos = ftello(gInFile);
	
	gIndex = NULL;
	while (pos > 0) {
		lzma_index *index = next_index(&pos);
		if (gIndex && lzma_index_cat(index, gIndex, NULL) != LZMA_OK)
			die("Error concatenating indices");
		gIndex = index;
	}
	
	return (gIndex != NULL);
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

size_t gPipelineProcessMax = 0;
size_t gPipelineQSize = 0;

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
	if (gPipelineProcessMax > 0 && gPipelineProcessMax < gPLProcessCount)
		gPLProcessCount = gPipelineProcessMax;
	
    gPLProcessThreads = malloc(gPLProcessCount * sizeof(pthread_t));
    int qsize = gPipelineQSize ? gPipelineQSize
        : ceil(gPLProcessCount * 1.3 + 1);
    if (qsize < gPLProcessCount) {
        fprintf(stderr, "Warning: queue size is less than thread count, "
            "performance will suffer!\n");
    }
    for (size_t i = 0; i < qsize; ++i) {
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

void pipeline_dispatch(pipeline_item_t *item, queue_t *q) {
    item->seq = gPLSplitSeq++;
    item->next = NULL;
    queue_push(q, PIPELINE_ITEM, item);
}

void pipeline_split(pipeline_item_t *item) {
	pipeline_dispatch(item, gPipelineSplitQ);
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
