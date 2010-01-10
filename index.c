#include "pixz.h"

#include <string.h>

#define CHUNKSIZE 4096 // Approximate


static bool pixz_index_is_prefix(const char *name);
static void pixz_index_add_record(pixz_index *i, size_t offset, const char *name);

static uint8_t *pixz_index_write_buf(pixz_index_record **rec, size_t *outsize);

pixz_index *pixz_index_new(void) {
    pixz_index *i = malloc(sizeof(pixz_index));
    i->first = NULL;
    i->last = NULL;
    i->have_last_offset = false;
    return i;
}

void pixz_index_free(pixz_index *i) {
    for (pixz_index_record *rec = i->first; rec; rec = rec->next) {
        free(rec->name);
        free(rec);
    }
    free(i);
}

static void pixz_index_add_record(pixz_index *i, size_t offset, const char *name) {
    pixz_index_record *rec = malloc(sizeof(pixz_index_record));
    rec->next = NULL;
    rec->name = name ? strdup(name) : NULL;
    rec->offset = offset;
    
    if (!i->first)
        i->first = rec;
    if (i->last)
        i->last->next = rec;
    i->last = rec;
}

void pixz_index_add(pixz_index *i, size_t offset, const char *name) {
    if (pixz_index_is_prefix(name)) {
        if (!i->have_last_offset)
            i->last_offset = offset;
        i->have_last_offset = true;
        return;
    }
    
    pixz_index_add_record(i, i->have_last_offset ? i->last_offset : offset, name);
    i->have_last_offset = false;
}

static bool pixz_index_is_prefix(const char *name) {
    // Unfortunately, this is the only way I can think of to identify
    // copyfile data.
    
    // basename(3) is not thread-safe
    size_t i = strlen(name);
    while (i != 0 && name[i - 1] != '/')
        --i;
    
    return strncmp(name + i, "._", 2) == 0;
}

void pixz_index_finish(pixz_index *i, size_t offset) {
    pixz_index_add_record(i, offset, NULL);
}

void pixz_index_dump(pixz_index *i, FILE *out) {
    pixz_index_record *rec;
    for (rec = i->first; rec && rec->name; rec = rec->next) {
        fprintf(out, "%12zu  %s\n", rec->offset, rec->name);
    }
    fprintf(out, "Total: %zu\n", rec->offset);
}

static uint8_t *pixz_index_write_buf(pixz_index_record **rec, size_t *outsize) {
    // How much space do we need?
    size_t space = 0;
    pixz_index_record *end = *rec;
    do {
        if (end->name)
            space += strlen(end->name);
        space += 2 + sizeof(uint64_t); // offset and two nulls
        end = end->next;
    } while (end && space < CHUNKSIZE);
    
    // Write it!
    uint8_t *buf, *pos;
    buf = pos = malloc(space);
    for (; *rec != end; *rec = (*rec)->next) {
        const char *name = (*rec)->name;
        if (!name)
            name  = "";
        printf("%s\n", name);
        
        size_t len = strlen(name);
        strncpy((char*)pos, name, len + 1);
        pos += len + 1;
        pixz_offset_write((*rec)->offset, pos);
        pos += sizeof(uint64_t);
        *pos++ = '\0';
    }
    
    *outsize = space;
    return buf;
}

fixme_err pixz_index_write(pixz_index *i, FILE *out, pixz_encode_options *opts) {
    lzma_block block;
    pixz_encode_initialize_block(&block, opts->check, opts->filters);
    
    uint8_t buf[CHUNKSIZE];
    pixz_encode_block_header(&block, buf, CHUNKSIZE);
    if (fwrite(buf, block.header_size, 1, out) != 1)
        pixz_die("Error writing file index header\n");
    
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret err = lzma_block_encoder(&stream, &block);
    if (err != LZMA_OK)
        pixz_die("Error #%d creating file index block encoder.\n", err);
    
    uint8_t *inbuf = NULL;
    stream.avail_in = 0;
    pixz_index_record *rec = i->first;
    while (rec) {
        if (stream.avail_in == 0) {
            free(inbuf);
            stream.next_in = inbuf = pixz_index_write_buf(&rec, &stream.avail_in);
            if (!inbuf) {
                if (lzma_code(&stream, LZMA_FINISH) != LZMA_STREAM_END)
                    pixz_die("Error finishing file index\n");
                break;
            }
        }
        
        stream.next_out = buf;
        stream.avail_out = CHUNKSIZE;        
        if (lzma_code(&stream, LZMA_RUN) != LZMA_OK)
            pixz_die("Error encoding file index\n");
        size_t wr = stream.next_out - buf;
        if (wr) {
            if (fwrite(buf, wr, 1, out) != 1)
                pixz_die("Error writing file index\n");
        }
    }
    lzma_end(&stream);
    
    return 31337;
}

fixme_err pixz_index_read_in_place(pixz_index **i, FILE *in) {
    return 31337;
}
