#include <lzma.h>

#define __USE_LARGEFILE 1

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <pthread.h>


#pragma mark DEFINES

#define PIXZ_INDEX_MAGIC 0xDBAE14D62E324CA6LL

#define CHECK LZMA_CHECK_CRC32
#define MEMLIMIT (64L * 1024 * 1024 * 1024) // crazy high

#define CHUNKSIZE 4096


#pragma mark UTILS

FILE *gInFile;
lzma_stream gStream;

extern lzma_index *gIndex;


void die(const char *fmt, ...);
char *xstrdup(const char *s);

uint64_t xle64dec(const uint8_t *d);
void xle64enc(uint8_t *d, uint64_t n);
size_t num_threads(void);

void *decode_block_start(off_t block_seek);


#pragma mark INDEX

typedef struct file_index_t file_index_t;
struct file_index_t {
    char *name;
    off_t offset;
    file_index_t *next;
};

extern file_index_t *gFileIndex, *gLastFile;

// As discovered from footer
extern lzma_check gCheck;

void decode_index(void);

lzma_vli find_file_index(void **bdatap);
bool read_file_index(void);
void dump_file_index(FILE *out);
void free_file_index(void);


#pragma mark QUEUE

typedef struct queue_item_t queue_item_t;
struct queue_item_t {
    int type;
    void *data;
    queue_item_t *next;
};

typedef void (*queue_free_t)(int type, void *p);

typedef struct {
    queue_item_t *first;
    queue_item_t *last;
    
    pthread_mutex_t mutex;
    pthread_cond_t pop_cond;
    
    queue_free_t freer;
} queue_t;


queue_t *queue_new(queue_free_t freer);
void queue_free(queue_t *q);
void queue_push(queue_t *q, int type, void *data);
int queue_pop(queue_t *q, void **datap);


#pragma mark PIPELINE

extern queue_t *gPipelineStartQ, *gPipelineSplitQ, *gPipelineMergeQ;

typedef enum {
    PIPELINE_ITEM,
    PIPELINE_STOP
} pipeline_tag_t;

typedef struct pipeline_item_t pipeline_item_t;
struct pipeline_item_t {
    size_t seq;
    pipeline_item_t *next;
    
    void *data;
};

typedef void* (*pipeline_data_create_t)(void);
typedef void (*pipeline_data_free_t)(void*);
typedef void (*pipeline_split_t)(void);
typedef void (*pipeline_process_t)(size_t);

void pipeline_create(
    pipeline_data_create_t create,
    pipeline_data_free_t destroy,
    pipeline_split_t split,
    pipeline_process_t process);
void pipeline_stop(void);
void pipeline_destroy(void);

void pipeline_split(pipeline_item_t *item);
pipeline_item_t *pipeline_merged();
