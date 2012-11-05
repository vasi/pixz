#include "pixz.h"

#include <archive.h>
#include <archive_entry.h>


#pragma mark DECLARE WANTED

typedef struct wanted_t wanted_t;
struct wanted_t {
    wanted_t *next;
    char *name;
    off_t start, end;
    size_t size;
};

static wanted_t *gWantedFiles = NULL;

static bool spec_match(char *spec, char *name);
static void wanted_files(size_t count, char **specs);
static void wanted_free(wanted_t *w);


#pragma mark DECLARE PIPELINE

typedef struct {
    uint8_t *input, *output;
	size_t incap, outcap;
    size_t insize, outsize;
    off_t uoffset; // uncompressed offset
	lzma_check check;
} io_block_t;

static void *block_create(void);
static void block_free(void *data);
static void read_thread(void);
static void read_thread_noindex(void);
static void decode_thread(size_t thnum);


#pragma mark DECLARE ARCHIVE

static pipeline_item_t *gArItem = NULL, *gArLastItem = NULL;
static off_t gArLastOffset;
static size_t gArLastSize;
static wanted_t *gArWanted = NULL;
static bool gArNextItem = false;

static int tar_ok(struct archive *ar, void *ref);
static ssize_t tar_read(struct archive *ar, void *ref, const void **bufp);
static bool tar_next_block(void);
static void tar_write_last(void);


#pragma mark DECLARE READ BUFFER

#define STREAMSIZE (1024 * 1024)

static pipeline_item_t *gRbufPI = NULL;
static io_block_t *gRbuf = NULL;

static void block_capacity(io_block_t *ib, size_t incap, size_t outcap);

typedef enum {
	RBUF_ERR, RBUF_EOF, RBUF_PART, RBUF_FULL
} rbuf_read_status;

static rbuf_read_status rbuf_read(size_t bytes);
static bool rbuf_cycle(lzma_stream *stream, bool start, size_t skip);
static void rbuf_consume(size_t bytes);
static void rbuf_dispatch(void);

static bool read_header(lzma_check *check);
static bool read_block(lzma_check check);
static void read_streaming(lzma_block *block);
static void read_index(void);
static void read_footer(void);


#pragma mark DECLARE UTILS

static lzma_vli gFileIndexOffset = 0;


#pragma mark MAIN

void pixz_read(bool verify, size_t nspecs, char **specs) {
    if (0 && decode_index()) { // FIXME
	    if (verify)
	        gFileIndexOffset = read_file_index();
	    wanted_files(nspecs, specs);    	
    }

#if DEBUG
    for (wanted_t *w = gWantedFiles; w; w = w->next)
        debug("want: %s", w->name);
#endif
    
    pipeline_create(block_create, block_free, read_thread_noindex, decode_thread);
    if (verify && gFileIndexOffset) {
		// FIXME: verify this works with noindex/streamed reading
		// FIXME: don't stop on End Of Archive
        gArWanted = gWantedFiles;
        wanted_t *w = gWantedFiles, *wlast = NULL;
        bool lastmulti = false;
        off_t lastoff = 0;
        
        struct archive *ar = archive_read_new();
        archive_read_support_compression_none(ar);
        archive_read_support_format_tar(ar);
        archive_read_open(ar, NULL, tar_ok, tar_read, tar_ok);
        struct archive_entry *entry;
        while (true) {
            int aerr = archive_read_next_header(ar, &entry);
            if (aerr == ARCHIVE_EOF) {
                break;
            } else if (aerr != ARCHIVE_OK && aerr != ARCHIVE_WARN) {
                fprintf(stderr, "%s\n", archive_error_string(ar));
                die("Error reading archive entry");
            }
            
            off_t off = archive_read_header_position(ar);
            const char *path = archive_entry_pathname(entry);
            if (!lastmulti) {
                if (wlast && wlast->size != off - lastoff)
                    die("Index and archive show differing sizes for %s: %d vs %d",
                        wlast->name, wlast->size, off - lastoff);
                lastoff = off;
            }
            
            lastmulti = is_multi_header(path);
            if (lastmulti)
                continue;
            
            if (!w)
                die("File %s missing in index", path);
            if (strcmp(path, w->name) != 0)
                die("Index and archive differ as to next file: %s vs %s",
                    w->name, path);
            
            wlast = w;
            w = w->next;
        }
        if (w && w->name)
            die("File %s missing in archive", w->name);
        tar_write_last(); // write whatever's left
    } else {
        pipeline_item_t *pi;
        while ((pi = pipeline_merged())) {
            io_block_t *ib = (io_block_t*)(pi->data);
            fwrite(ib->output, ib->outsize, 1, gOutFile);
            queue_push(gPipelineStartQ, PIPELINE_ITEM, pi);
        }
    }
    
    pipeline_destroy();
    wanted_free(gWantedFiles);
}


#pragma mark BLOCKS

static void *block_create(void) {
    io_block_t *ib = malloc(sizeof(io_block_t));
	ib->incap = ib->outcap = 0;
	ib->input = ib->output = NULL;
    return ib;
}

static void block_free(void* data) {
    io_block_t *ib = (io_block_t*)data;
    free(ib->input);
    free(ib->output);
    free(ib);
}


#pragma mark SETUP

static void wanted_free(wanted_t *w) {
    for (wanted_t *w = gWantedFiles; w; ) {
        wanted_t *tmp = w->next;
        free(w);
        w = tmp;
    }
}


static bool spec_match(char *spec, char *name) {
    bool match = true;
    for (; *spec; ++spec, ++name) {
        if (!*name || *spec != *name) { // spec must be equal or prefix
            match = false;
            break;
        }
    }
    // If spec's a prefix of the file name, it must be a dir name
    return match && (!*name || *name == '/');
}

static void wanted_files(size_t count, char **specs) {
    if (!gFileIndexOffset) {
        if (count)
            die("Can't filter non-tarball");
        gWantedFiles = NULL;
        return;
    }
    
    // Remove trailing slashes from specs
    for (char **spec = specs; spec < specs + count; ++spec) {
        char *c = *spec;
        while (*c++) ; // forward to end
        while (--c >= *spec && *c == '/')
            *c = '\0';
    }
    
    bool matched[count];  // for each spec, does it match?
    memset(matched, 0, sizeof(matched));
    wanted_t *last = NULL;
    
    // Check each file in order, to see if we want it
    for (file_index_t *f = gFileIndex; f->name; f = f->next) {
        bool match = !count;
        for (char **spec = specs; spec < specs + count; ++spec) {
            if (spec_match(*spec, f->name)) {
                match = true;
                matched[spec - specs] = true;
                break;
            }
        }
        
        if (match) {
            wanted_t *w = malloc(sizeof(wanted_t));
            *w = (wanted_t){ .name = f->name, .start = f->offset,
                .end = f->next->offset, .next = NULL };
            w->size = w->end - w->start;
            if (last) {
                last->next = w;
            } else {
                gWantedFiles = w;
            }
            last = w;
        }
    }
    
    // Make sure each spec matched
    for (size_t i = 0; i < count; ++i) {
        if (!matched[i])
            die("\"%s\" not found in archive", *(specs + i));
    }
}


#pragma mark READ

static void block_capacity(io_block_t *ib, size_t incap, size_t outcap) {
	if (incap > ib->incap) {
		ib->incap = incap;
		ib->input = realloc(ib->input, incap);
	}
	if (outcap > ib->outcap) {
		ib->outcap = outcap;
		ib->output = malloc(outcap);
	}
}

// Ensure at least this many bytes available
// Return 1 on success, zero on EOF, -1 on error
static rbuf_read_status rbuf_read(size_t bytes) {
	if (!gRbufPI) {
        queue_pop(gPipelineStartQ, (void**)&gRbufPI);
		gRbuf = (io_block_t*)(gRbufPI->data);
		gRbuf->insize = gRbuf->outsize = 0;
	}
	
	if (gRbuf->insize >= bytes)
		return RBUF_FULL;
	
	block_capacity(gRbuf, bytes, 0);
	size_t r = fread(gRbuf->input + gRbuf->insize, 1, bytes - gRbuf->insize,
		gInFile);
	gRbuf->insize += r;
	
	if (r)
		return (gRbuf->insize == bytes) ? RBUF_FULL : RBUF_PART;
	return feof(gInFile) ? RBUF_EOF : RBUF_ERR;
}

static bool rbuf_cycle(lzma_stream *stream, bool start, size_t skip) {
	if (!start) {
		rbuf_consume(gRbuf->insize);
		if (rbuf_read(CHUNKSIZE) < RBUF_PART)
			return false;
	}
	stream->next_in = gRbuf->input + skip;
	stream->avail_in = gRbuf->insize - skip;
	return true;
}

static void rbuf_consume(size_t bytes) {
	if (bytes < gRbuf->insize)
		memmove(gRbuf->input, gRbuf->input + bytes, gRbuf->insize - bytes);
	gRbuf->insize -= bytes;
}

static void rbuf_dispatch(void) {
	pipeline_split(gRbufPI);
	gRbufPI = NULL;
	gRbuf = NULL;
}


static bool read_header(lzma_check *check) {
	lzma_stream_flags stream_flags;
	rbuf_read_status st = rbuf_read(LZMA_STREAM_HEADER_SIZE);
	if (st == RBUF_EOF)
		return false;
	else if (st != RBUF_FULL)
		die("Error reading stream header");
	lzma_ret err = lzma_stream_header_decode(&stream_flags, gRbuf->input);
	if (err == LZMA_FORMAT_ERROR)
		die("Not an XZ file");
	else if (err != LZMA_OK)
		die("Error decoding XZ header");
	*check = stream_flags.check;
	rbuf_consume(LZMA_STREAM_HEADER_SIZE);
	return true;
}

static bool read_block(lzma_check check) {
    lzma_filter filters[LZMA_FILTERS_MAX + 1];
    lzma_block block = { .filters = filters, .check = check, .version = 0 };
	
	if (rbuf_read(1) != RBUF_FULL)
		die("Error reading block header size");
	if (gRbuf->input[0] == 0)
		return false;
	
	block.header_size = lzma_block_header_size_decode(gRbuf->input[0]);
	if (block.header_size > LZMA_BLOCK_HEADER_SIZE_MAX)
		die("Block header size too large");
	if (rbuf_read(block.header_size) != RBUF_FULL)
		die("Error reading block header");
	if (lzma_block_header_decode(&block, NULL, gRbuf->input) != LZMA_OK)
		die("Error decoding block header");
		
	size_t comp = block.compressed_size, outsize = block.uncompressed_size;
	if (comp == LZMA_VLI_UNKNOWN || outsize == LZMA_VLI_UNKNOWN) {
		read_streaming(&block);
	} else {
		block_capacity(gRbuf, 0, outsize);
		gRbuf->outsize = outsize;
		gRbuf->check = check;
		
		if (rbuf_read(lzma_block_total_size(&block)) != RBUF_FULL)
			die("Error reading block contents");
		rbuf_dispatch();
	}
	return true;
}

static void read_streaming(lzma_block *block) {
    lzma_stream stream = LZMA_STREAM_INIT;
    if (lzma_block_decoder(&stream, block) != LZMA_OK)
		die("Error initializing streaming block decode");
	rbuf_cycle(&stream, true, block->header_size);
	stream.avail_out = 0;
	
    pipeline_item_t *pi = NULL;
    io_block_t *ib = NULL;
    
	lzma_ret err = LZMA_OK;
	while (err != LZMA_STREAM_END) {
		if (err != LZMA_OK)
			die("Error decoding streaming block");
		
		if (stream.avail_out == 0) {
			if (ib) {
				ib->outsize = ib->outcap;
				pipeline_dispatch(pi, gPipelineMergeQ);
			}
			queue_pop(gPipelineStartQ, (void**)&pi);
			ib = (io_block_t*)pi->data;
			block_capacity(ib, 0, STREAMSIZE);
			stream.next_out = ib->output;
			stream.avail_out = ib->outcap;
		}
		if (stream.avail_in == 0 && !rbuf_cycle(&stream, false, 0))
			die("Error reading streaming block");
		
		err = lzma_code(&stream, LZMA_RUN);
	}
	
	if (ib && stream.avail_out != ib->outcap) {
		ib->outsize = ib->outcap - stream.avail_out;
		pipeline_dispatch(pi, gPipelineMergeQ);
	}
	rbuf_consume(gRbuf->insize - stream.avail_in);
	lzma_end(&stream);
}

static void read_index(void) {
	// FIXME: verify it matches the blocks?
    lzma_stream stream = LZMA_STREAM_INIT;
	lzma_index *index;
	if (lzma_index_decoder(&stream, &index, MEMLIMIT) != LZMA_OK)
		die("Error initializing index decoder");
	rbuf_cycle(&stream, true, 0);
	
	lzma_ret err = LZMA_OK;
	while (err != LZMA_STREAM_END) {
		if (err != LZMA_OK)
			die("Error decoding index");
		if (stream.avail_in == 0 && !rbuf_cycle(&stream, false, 0))
			die("Error reading index");
		err = lzma_code(&stream, LZMA_RUN);
	}
	rbuf_consume(gRbuf->insize - stream.avail_in);
	lzma_end(&stream);
}

static void read_footer(void) {
	// FIXME: compare with header?
	lzma_stream_flags stream_flags;
	if (rbuf_read(LZMA_STREAM_HEADER_SIZE) != RBUF_FULL)
		die("Error reading stream footer");
	if (lzma_stream_footer_decode(&stream_flags, gRbuf->input) != LZMA_OK)
		die("Error decoding XZ footer");
	rbuf_consume(LZMA_STREAM_HEADER_SIZE);
	
	char zeros[4] = "\0\0\0\0";
	while (true) {
		rbuf_read_status st = rbuf_read(4);
		if (st == RBUF_EOF)
			return;
		if (st != RBUF_FULL)
			die("Footer must be multiple of four bytes");
		if (memcmp(zeros, gRbuf->input, 4) != 0)
			return;
		rbuf_consume(4);
	}
}

static void read_thread_noindex(void) {
	bool empty = true;
	lzma_check check = LZMA_CHECK_NONE;
	while (read_header(&check)) {
		empty = false;
		while (read_block(check))
			; // pass
		read_index();
		read_footer();
		// FIXME: don't output the pixz file index! heuristic?
	}
	if (empty)
		die("Empty input");
	pipeline_stop();
}

static void read_thread(void) {
    off_t offset = ftello(gInFile);
    wanted_t *w = gWantedFiles;
    
    lzma_index_iter iter;
    lzma_index_iter_init(&iter, gIndex);
    while (!lzma_index_iter_next(&iter, LZMA_INDEX_ITER_BLOCK)) {
        // Don't decode the file-index
        off_t boffset = iter.block.compressed_file_offset;
        size_t bsize = iter.block.total_size;
        if (gFileIndexOffset && boffset == gFileIndexOffset)
            continue;
        
        // Do we need this block?
        if (gWantedFiles) {
            off_t uend = iter.block.uncompressed_file_offset +
                iter.block.uncompressed_size;
            if (!w || w->start >= uend) {
                debug("read: skip %llu", iter.block.number_in_file);
                continue;
            }
            for ( ; w && w->end < uend; w = w->next) ;
        }
        debug("read: want %llu", iter.block.number_in_file);
        
        // Get a block to work with
        pipeline_item_t *pi;
        queue_pop(gPipelineStartQ, (void**)&pi);
        io_block_t *ib = (io_block_t*)(pi->data);
		block_capacity(ib, iter.block.unpadded_size,
			iter.block.uncompressed_size);
        
        // Seek if needed, and get the data
        if (offset != boffset) {
            fseeko(gInFile, boffset, SEEK_SET);
            offset = boffset;
        }        
        ib->insize = fread(ib->input, 1, bsize, gInFile);
        if (ib->insize < bsize)
            die("Error reading block contents");
        offset += bsize;
        ib->uoffset = iter.block.uncompressed_file_offset;
		ib->check = iter.stream.flags->check;
        
        pipeline_split(pi);
    }
    
    pipeline_stop();
}

#pragma mark DECODE

static void decode_thread(size_t thnum) {
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_filter filters[LZMA_FILTERS_MAX + 1];
    lzma_block block = { .filters = filters, .check = LZMA_CHECK_NONE,
		.version = 0 };
    
    pipeline_item_t *pi;
    io_block_t *ib;
    
    while (PIPELINE_STOP != queue_pop(gPipelineSplitQ, (void**)&pi)) {
        ib = (io_block_t*)(pi->data);
        
        block.header_size = lzma_block_header_size_decode(*(ib->input));
        block.check = ib->check;
		if (lzma_block_header_decode(&block, NULL, ib->input) != LZMA_OK)
            die("Error decoding block header");
        if (lzma_block_decoder(&stream, &block) != LZMA_OK)
            die("Error initializing block decode");
        
        stream.avail_in = ib->insize - block.header_size;
        stream.next_in = ib->input + block.header_size;
        stream.avail_out = ib->outcap;
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


#pragma mark ARCHIVE

static int tar_ok(struct archive *ar, void *ref) {
    return ARCHIVE_OK;
}

static bool tar_next_block(void) {
    if (gArItem && !gArNextItem && gArWanted) {
        io_block_t *ib = (io_block_t*)(gArItem->data);
        if (gArWanted->start < ib->uoffset + ib->outsize)
            return true; // No need
    }
    
    if (gArLastItem)
        queue_push(gPipelineStartQ, PIPELINE_ITEM, gArLastItem);
    gArLastItem = gArItem;
    gArItem = pipeline_merged();
    gArNextItem = false;
    return gArItem;
}

static void tar_write_last(void) {
    if (gArItem) {
        io_block_t *ib = (io_block_t*)(gArItem->data);
        fwrite(ib->output + gArLastOffset, gArLastSize, 1, gOutFile);
        gArLastSize = 0;
    }
}

static ssize_t tar_read(struct archive *ar, void *ref, const void **bufp) {
    // If we got here, the last bit of archive is ok to write
    tar_write_last();
        
    // Write the first wanted file
    if (!tar_next_block())
        return 0;
    
    off_t off;
    size_t size;
    io_block_t *ib = (io_block_t*)(gArItem->data);
    if (gWantedFiles) {
        debug("tar want: %s", gArWanted->name);
        off = gArWanted->start - ib->uoffset;
        size = gArWanted->size;
        if (off < 0) {
            size += off;
            off = 0;
        }
        if (off + size > ib->outsize) {
            size = ib->outsize - off;
            gArNextItem = true; // force the end of this block
        } else {
            gArWanted = gArWanted->next;
        }
    } else {
        off = 0;
        size = ib->outsize;
    }
    debug("tar off = %llu, size = %zu", (unsigned long long)off, size);
    
    gArLastOffset = off;
    gArLastSize = size;
    if (bufp)
        *bufp = ib->output + off;
    return size;
}
