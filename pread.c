#include "pixz.h"

#include <getopt.h>

/* TODO
 * - restrict to certain files
 * - verify file-index matches archive contents
 */

typedef struct wanted_t wanted_t;
struct wanted_t {
    wanted_t *next;
    char *name;
    size_t offset;
    size_t size;
};

static wanted_t *gWantedFiles = NULL;

static void wanted_files(size_t count, char **specs);
static void wanted_free(wanted_t *w);


typedef struct {
    uint8_t *input, *output;
    size_t insize, outsize;
} io_block_t;

static void *block_create(void);
static void block_free(void *data);
static void read_thread(void);
static void decode_thread(size_t thnum);


static char **gFileSpecs, **gFileSpecEnd;

static FILE *gOutFile;
static lzma_vli gFileIndexOffset = 0;
static size_t gBlockInSize = 0, gBlockOutSize = 0;

static void set_block_sizes(void);


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
    
    // Set up file index
    gFileIndexOffset = read_file_index();
    wanted_files(argc - optind, argv + optind);
    for (wanted_t *w = gWantedFiles; w; w = w->next)
        printf("want: %s\n", w->name);
    exit(0);
    
    set_block_sizes();
    pipeline_create(block_create, block_free, read_thread, decode_thread);
    pipeline_item_t *pi;
    while ((pi = pipeline_merged())) {
        io_block_t *ib = (io_block_t*)(pi->data);
        fwrite(ib->output, ib->outsize, 1, gOutFile);
        queue_push(gPipelineStartQ, PIPELINE_ITEM, pi);
    }
    pipeline_destroy();
    
    wanted_free(gWantedFiles);
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
        // Don't decode the file-index
        size_t boffset = iter.block.compressed_file_offset,
            bsize = iter.block.total_size;
        if (gFileIndexOffset && boffset == gFileIndexOffset)
            continue;
        
        // Get a block to work with
        pipeline_item_t *pi;
        queue_pop(gPipelineStartQ, (void**)&pi);
        io_block_t *ib = (io_block_t*)(pi->data);
        
        // Seek if needed, and get the data
        if (offset != boffset) {
            fseeko(gInFile, boffset, SEEK_SET);
            offset = boffset;
        }        
        ib->insize = fread(ib->input, 1, bsize, gInFile);
        if (ib->insize < bsize)
            die("Error reading block contents");
        offset += bsize;
        
        pipeline_split(pi);
    }
    
    pipeline_stop();
}

static void wanted_free(wanted_t *w) {
    for (wanted_t *w = gWantedFiles; w; w = w->next) {
        wanted_t *tmp = w->next;
        free(w);
        w = tmp;
    }
}

static void wanted_files(size_t count, char **specs) {
    if (count == 0) {
        gWantedFiles = NULL;
        return;
    }
    if (!gFileIndexOffset)
        die("Can't filter non-tarball");
    
    // Remove trailing slashes from specs
    for (char **spec = specs; spec < specs + count; ++spec) {
        char *c = *spec;
        while (*c++) ; // forward to end
        while (--c >= *spec && *c == '/')
            *c = '\0';
    }
    
    wanted_t *last = NULL;
    for (file_index_t *f = gFileIndex; f->name; f = f->next) {
        // Do we want this file?
        for (char **spec = specs; spec < specs + count; ++spec) {
            char *sc, *nc;
            bool match = true;
            for (sc = *spec, nc = f->name; *sc; ++sc, ++nc) {
                if (!*nc || *sc != *nc) { // spec must be equal or prefix
                    match = false;
                    break;
                }
            }
            if (match && (!*nc || *nc == '/')) { // prefix must be at dir bound
                wanted_t *w = malloc(sizeof(wanted_t));
                *w = (wanted_t){ .name = f->name, .offset = f->offset,
                    .size = f->next->offset - f->offset, .next = NULL };
                if (last) {
                    last->next = w;
                } else {
                    gWantedFiles = w;
                }
                last = w;
                break;
            }
        }
    }
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
