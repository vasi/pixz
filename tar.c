#include "pixz.h"

#include <archive.h>
#include <archive_entry.h>

#include <sys/errno.h>

#define CHUNKSIZE 4096

typedef struct {
    FILE *file;
    uint8_t buf[CHUNKSIZE];
} pixz_tar;

static int pixz_tar_open(struct archive *a, void *refp);
static int pixz_tar_close(struct archive *a, void *refp);
static ssize_t pixz_tar_read(struct archive *a, void *refp, const void **buf);

#include <string.h>

int main(void) {
    pixz_index *index = pixz_index_new();
    
    struct archive *a = archive_read_new();
    archive_read_support_compression_none(a);
    archive_read_support_format_tar(a);
    
    FILE *infile = stdin;
    pixz_tar input = { .file = infile };    
    if (archive_read_open(a, &input, pixz_tar_open, pixz_tar_read,
            pixz_tar_close) != ARCHIVE_OK)
        pixz_die("Can't open archive\n");
    
    while (1) {
        struct archive_entry *entry;
        int aerr = archive_read_next_header(a, &entry);
        if (aerr == ARCHIVE_EOF) {
            pixz_index_finish(index, ftello(stdin));
            break;
        } else if (aerr != ARCHIVE_OK && aerr != ARCHIVE_WARN) {
            // libarchive warns for silly things like failure to convert
            // names into multibyte strings
            pixz_die("Error reading header: %s\n", archive_error_string(a));
        }
        
        const char *name = archive_entry_pathname(entry);
        size_t offset = archive_read_header_position(a);
        pixz_index_add(index, offset, name);
        
        if (archive_read_data_skip(a) != ARCHIVE_OK)
            pixz_die("Error skipping data\n");        
    }
    if (archive_read_finish(a) != ARCHIVE_OK)
        pixz_die("Error finishing read\n");
    
    FILE *ifile = fopen(INDEXFILE, "w+");
    if (!ifile)
        pixz_die("Can't open index file\n");
    pixz_encode_options *opts = pixz_encode_options_new();
    pixz_encode_options_default(opts);
    pixz_index_write(index, ifile, opts);
    pixz_index_free(index);
    lzma_check check = opts->check;
    pixz_encode_options_free(opts);
    
    fseek(ifile, 0, SEEK_SET);
    pixz_index *i2;
    pixz_index_read_in_place(&i2, ifile, check);
    fclose(ifile);
    
    pixz_index_dump(i2, stdout);
    pixz_index_free(i2);
    
    return 0;
}


static int pixz_tar_open(struct archive *a, void *refp) {
    return ARCHIVE_OK;
}

static int pixz_tar_close(struct archive *a, void *refp) {
    fclose(((pixz_tar*)refp)->file);
    return ARCHIVE_OK;
}

static ssize_t pixz_tar_read(struct archive *a, void *refp, const void **buf) {
    pixz_tar *input = (pixz_tar*)refp;
    size_t rd = fread(input->buf, 1, CHUNKSIZE, input->file);
    if (ferror(input->file)) {
        archive_set_error(a, errno, "Read error");
        return -1;
    }
    *buf = input->buf;
    return rd;
}
