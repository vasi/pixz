#include "pixz.h"

#include <getopt.h>

/* TODO
 * - restrict to certain files
 * - verify file-index matches archive contents
 */

typedef struct {
    uint8_t *input, *output;
    size_t insize, outsize;
} io_block_t;

static void *block_create(void);
static void block_free(void *data);
static void read_thread(void);
static void decode_thread(size_t thnum);


static FILE *gOutFile;
static lzma_vli gFileIndexOffset = 0;
static size_t gBlockInSize = 0, gBlockOutSize = 0;

static void set_block_sizes();


int main(int argc, char **argv) {
    gInFile = stdin;
    gOutFile = stdout;
    int ch;
    while ((ch = getopt(argc, argv, "i:o:")) != -1) {
        switch (ch) {
            case 'i':
                if (!(gInFile = fopen(optarg, "r")))
                    die ("Can't open input file");
                break;
            case 'o':
                if (!(gOutFile = fopen(optarg, "w")))
                    die ("Can't open output file");
                break;
            default:
                die("Unknown option");
        }
    }
    argc -= optind - 1;
    argv += optind - 1;
      
    
    // Find block sizes
    gFileIndexOffset = find_file_index(NULL);
    set_block_sizes();
    
    pipeline_create(block_create, block_free, read_thread, decode_thread);
    pipeline_item_t *pi;
    while ((pi = pipeline_merged())) {
        io_block_t *ib = (io_block_t*)(pi->data);
        fwrite(ib->output, ib->outsize, 1, gOutFile);
        queue_push(gPipelineStartQ, PIPELINE_ITEM, pi);
    }
    pipeline_destroy();
    
    return 0;
}

static void *block_create(void) {
    io_block_t *ib = malloc(sizeof(io_block_t));
    ib->input = malloc(gBlockInSize);
    ib->output = malloc(gBlockOutSize);
    return ib;
}

static void block_free(void* data) {
    io_block_t *ib = (io_block_t*)data;
    free(ib->input);
    free(ib->output);
    free(ib);
}

static void set_block_sizes() {
    lzma_index_iter iter;
    lzma_index_iter_init(&iter, gIndex);
    while (!lzma_index_iter_next(&iter, LZMA_INDEX_ITER_BLOCK)) {
        // exclude the file index block
        lzma_vli off = iter.block.compressed_file_offset;
        if (gFileIndexOffset && off == gFileIndexOffset)
            continue;
        
        size_t in = iter.block.total_size,
            out = iter.block.uncompressed_size;
        if (out > gBlockOutSize)
            gBlockOutSize = out;
        if (in > gBlockInSize)
            gBlockInSize = in;
    }
}

static void read_thread(void) {
    off_t offset = ftello(gInFile);
    
    lzma_index_iter iter;
    lzma_index_iter_init(&iter, gIndex);
    while (!lzma_index_iter_next(&iter, LZMA_INDEX_ITER_BLOCK)) {
        size_t boffset = iter.block.compressed_file_offset;
        if (boffset == gFileIndexOffset)
            continue;
        
        pipeline_item_t *pi;
        queue_pop(gPipelineStartQ, (void**)&pi);
        io_block_t *ib = (io_block_t*)(pi->data);
        
        if (offset != boffset) {
            fseeko(gInFile, boffset, SEEK_SET);
            offset = boffset;
        }
        
        size_t bsize = iter.block.total_size;
        ib->insize = fread(ib->input, 1, bsize, gInFile);
        if (ib->insize < bsize)
            die("Error reading block contents");
        
        pipeline_split(pi);
    }
    
    pipeline_stop();
}

static void decode_thread(size_t thnum) {
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_filter filters[LZMA_FILTERS_MAX + 1];
    lzma_block block = { .filters = filters, .check = gCheck, .version = 0 };
    
    pipeline_item_t *pi;
    io_block_t *ib;
    
    while (PIPELINE_STOP != queue_pop(gPipelineSplitQ, (void**)&pi)) {
        ib = (io_block_t*)(pi->data);
        
        block.header_size = lzma_block_header_size_decode(*(ib->input));
        if (lzma_block_header_decode(&block, NULL, ib->input) != LZMA_OK)
            die("Error decoding block header");
        if (lzma_block_decoder(&stream, &block) != LZMA_OK)
            die("Error initializing block decode");
        
        stream.avail_in = ib->insize - block.header_size;
        stream.next_in = ib->input + block.header_size;
        stream.avail_out = gBlockOutSize;
        stream.next_out = ib->output;
        
        lzma_ret err = LZMA_OK;
        while (err != LZMA_STREAM_END) {
            if (err != LZMA_OK)
                die("Error decoding block");
            err = lzma_code(&stream, LZMA_FINISH);
        }
        
        ib->outsize = stream.next_out - ib->output;
        queue_push(gPipelineMergeQ, PIPELINE_ITEM, pi);
    }
    lzma_end(&stream);
}

