#include "pixz.h"


#pragma mark FUNCTION DECLARATIONS

static void extract_file(const char *target);
static void extract_block(off_t block_seek, off_t skip, off_t size);


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

static void extract_file(const char *target) {
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

static void extract_block(off_t block_seek, off_t skip, off_t size) {    
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
