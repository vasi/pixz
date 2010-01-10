#include "pixz.h"

#include <string.h>

#define CHUNKSIZE 32


static bool pixz_index_is_prefix(const char *name);
static void pixz_index_add_record(pixz_index *i, size_t offset, const char *name);

typedef struct pixz_index_write_state pixz_index_write_state;
static size_t pixz_index_write_buf(pixz_index_write_state *state, uint8_t *buf);

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

typedef enum {
    PIXZ_WRITE_NAME,
    PIXZ_WRITE_LONG_NAME,
    PIXZ_WRITE_SIZE,
} pixz_index_write_part;

struct pixz_index_write_state {
    pixz_index_write_part part;
    pixz_index_record *rec;
    size_t namepos; // Position within a long-name
    size_t namelen; // Total length of a long-name
};

static size_t pixz_index_write_buf(pixz_index_write_state *state, uint8_t *buf) {
    uint8_t *end = buf + CHUNKSIZE;
    bool done = false;
    while (!done) {
        switch (state->part) {
            case PIXZ_WRITE_SIZE:
                if (buf + sizeof(uint64_t) + 1 > end) {
                    done = true;
                } else {
                    pixz_offset_write(state->rec->offset, buf);
                    buf += sizeof(uint64_t);
                    *buf++ = '\0';
                    
                    state->rec = state->rec->next;
                    state->part = PIXZ_WRITE_NAME;
                }
                break;
            
            case PIXZ_WRITE_NAME:
                if (!state->rec) {
                    done = true;
                } else { // We have a record
                    const char *name = state->rec->name;
                    if (!name)
                        name = ""; // End record
                    printf("%s\n", name);
                    
                    size_t len = strlen(name) + 1;
                    if (len > CHUNKSIZE) {
                        state->namelen = len;
                        state->namepos = 0;
                        state->part = PIXZ_WRITE_LONG_NAME;
                    } else {
                        memcpy(buf, name, len);
                        buf += len;
                        state->part = PIXZ_WRITE_SIZE;
                    }
                }
                break;
            
            case PIXZ_WRITE_LONG_NAME: {
                    size_t todo = state->namelen + 1 - state->namepos;
                    if (todo > end - buf)
                        todo = end - buf;
                    memcpy(buf, state->rec->name + state->namepos, todo);
                    buf += todo;
                    state->namepos += todo;
                    
                    state->part = state->namepos == state->namelen
                        ? PIXZ_WRITE_SIZE : PIXZ_WRITE_LONG_NAME;
                    done = true;
                }
                break;
        }
    }
    return CHUNKSIZE - (end - buf);
}

fixme_err pixz_index_write(pixz_index *i, FILE *out, pixz_encode_options *opts) {
    lzma_block block;
    pixz_encode_initialize_block(&block, opts->check, opts->filters);
    
    uint8_t inbuf[CHUNKSIZE], outbuf[CHUNKSIZE];
    pixz_encode_block_header(&block, outbuf, CHUNKSIZE);
    if (fwrite(outbuf, block.header_size, 1, out) != 1)
        pixz_die("Error writing file index header\n");
    
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret err = lzma_block_encoder(&stream, &block);
    if (err != LZMA_OK)
        pixz_die("Error #%d creating file index block encoder.\n", err);
    stream.avail_in = 0;
    
    pixz_index_write_state state = { .part = PIXZ_WRITE_NAME,
        .rec = i->first, .namepos = 0 };
    while (true) {
        if (stream.avail_in == 0) {
            stream.avail_in = pixz_index_write_buf(&state, inbuf);
            if (stream.avail_in == 0) {
                if (lzma_code(&stream, LZMA_FINISH) != LZMA_STREAM_END)
                    pixz_die("Error finishing file index\n");
                break;
            }
            stream.next_in = inbuf;
        }
        
        stream.next_out = outbuf;
        stream.avail_out = CHUNKSIZE;        
        if (lzma_code(&stream, LZMA_RUN) != LZMA_OK)
            pixz_die("Error encoding file index\n");
        if (stream.next_out != outbuf) {
            printf("%ld\n", stream.next_out - outbuf);
            if (fwrite(outbuf, stream.next_out - outbuf, 1, out) != 1)
                pixz_die("Error writing file index\n");
        }
    }
    
    return 31337;
}

fixme_err pixz_index_read_in_place(pixz_index **i, FILE *in) {
    uint8_t inbuf[CHUNKSIZE], outbuf[CHUNKSIZE];
    
    
}
