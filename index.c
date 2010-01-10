#include "pixz.h"

#include <string.h>

#define CHUNKSIZE 4096 // Approximate


static bool pixz_index_is_prefix(const char *name);
static void pixz_index_add_record(pixz_index *i, size_t offset, const char *name);

static uint8_t *pixz_index_write_buf(pixz_index_record **rec, size_t *outsize);
static size_t pixz_index_read_buf(pixz_index *i, uint8_t **outbuf,
        uint8_t * last, size_t *outsize);

pixz_index *pixz_index_new(void) {
    pixz_index *i = malloc(sizeof(pixz_index));
    i->first = NULL;
    i->last = NULL;
    i->have_last_offset = false;
    return i;
}

void pixz_index_free(pixz_index *i) {
    pixz_index_record *nextrec;
    for (pixz_index_record *rec = i->first; rec; rec = nextrec) {
        free(rec->name);
        nextrec = rec->next;
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
        space += 1 + sizeof(uint64_t); // nul and offset
        end = end->next;
    } while (end && space < CHUNKSIZE);
    
    // Write it!
    uint8_t *buf, *pos;
    buf = pos = malloc(space);
    for (; *rec != end; *rec = (*rec)->next) {
        const char *name = (*rec)->name;
        if (!name)
            name  = ""; // Empty string signifies finish
        
        size_t len = strlen(name);
        strncpy((char*)pos, name, len + 1);
        pos += len + 1;
        pixz_offset_write((*rec)->offset, pos);
        pos += sizeof(uint64_t);
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
    if (lzma_block_encoder(&stream, &block) != LZMA_OK)
        pixz_die("Error creating file index block encoder.\n");
    
    uint8_t *inbuf = NULL;
    pixz_index_record *rec = i->first;
    stream.avail_in = 0;
    lzma_ret err = LZMA_OK;
    lzma_action action = LZMA_RUN;
    while (err != LZMA_STREAM_END) {
        if (action != LZMA_FINISH && stream.avail_in == 0) {
            free(inbuf);
            stream.next_in = inbuf = pixz_index_write_buf(&rec, &stream.avail_in);
            action = rec ? LZMA_RUN : LZMA_FINISH;
        }
        
        stream.next_out = buf;
        stream.avail_out = CHUNKSIZE;
        err = lzma_code(&stream, action);
        if (err != LZMA_OK && err != LZMA_STREAM_END)
            pixz_die("Error #%d encoding file index\n", err);
        
        size_t wr = stream.next_out - buf;
        if (wr) {
            if (fwrite(buf, wr, 1, out) != 1)
                pixz_die("Error writing file index\n");
        }
    }
    free(inbuf);
    lzma_end(&stream);
    
    return 31337;
}

// return number of bytes at beginning to keep
static size_t pixz_index_read_buf(pixz_index *i, uint8_t **outbuf,
        uint8_t *last, size_t *outsize) {
    uint8_t *pos = *outbuf, *lastpos = last - sizeof(uint64_t);
    while (pos < lastpos) {        
        uint8_t *strend = memchr(pos, '\0', lastpos - pos);
        if (!strend)
            break;
        
        uint64_t offset = pixz_offset_read(strend + 1);
        if (*pos) {
            pixz_index_add_record(i, offset, (char*)pos);
        } else {
            pixz_index_finish(i, offset);
            return 0;
        }
        pos = strend + 1 + sizeof(uint64_t);
    }
    
    if (pos == *outbuf) {
        // found nothing at all, need a bigger buffer
        size_t oldsize = *outsize;
        *outsize *= 2;
        *outbuf = realloc(*outbuf, *outsize);
        return oldsize;
    } else {
        size_t keep = last - pos;
        memmove(*outbuf, pos, keep);
        return keep;
    }
}

fixme_err pixz_index_read_in_place(pixz_index **i, FILE *in, lzma_check check) {
    int c = fgetc(in);
    if (c == EOF || c == 0)
        pixz_die("There's no block here\n");
    
    lzma_block block = { .check = check };
    block.header_size = lzma_block_header_size_decode(c);
    uint8_t header[block.header_size];
    header[0] = c;
    if (fread(header + 1, block.header_size - 1, 1, in) != 1)
        pixz_die("Can't read block header\n");
        
    block.filters = malloc((LZMA_FILTERS_MAX + 1) * sizeof(lzma_filter));
    if (lzma_block_header_decode(&block, NULL, header) != LZMA_OK)
        pixz_die("Can't decode header\n");
    
    lzma_stream stream = LZMA_STREAM_INIT;
    if (lzma_block_decoder(&stream, &block) != LZMA_OK)
        pixz_die("Can't setup block decoder\n");
    
    size_t outsize = CHUNKSIZE;
    uint8_t inbuf[CHUNKSIZE], *outbuf = malloc(outsize);
    
    *i = pixz_index_new();
    stream.next_out = outbuf;
    stream.avail_out = outsize;
    stream.avail_in = 0;
    lzma_ret err = LZMA_OK;
    lzma_action action = LZMA_RUN;
    while (err != LZMA_STREAM_END) {
        if (action != LZMA_FINISH && stream.avail_in == 0) {
            stream.avail_in = fread(inbuf, 1, CHUNKSIZE, in);
            stream.next_in = inbuf;
            action = stream.avail_in == 0 ? LZMA_FINISH : LZMA_RUN;
        }
        
        err = lzma_code(&stream, action);
        if (err != LZMA_OK && err != LZMA_STREAM_END)
            pixz_die("Error #%d decoding file index\n", err);

        if (stream.avail_out == 0 || err == LZMA_STREAM_END) {
            size_t keep = pixz_index_read_buf(*i, &outbuf, stream.next_out, &outsize);
            stream.next_out = outbuf + keep;
            stream.avail_out = outsize - keep;
        }
    }
    free(block.filters);
    free(outbuf);
    lzma_end(&stream);
    
    return 31337;
}
