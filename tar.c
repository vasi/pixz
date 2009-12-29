#include "pixz.h"

#include <archive.h>
#include <archive_entry.h>

#include <sys/errno.h>
#include <string.h>
#include <libgen.h>


// Tar uses records of 512 bytes
#define CHUNKSIZE 512


typedef struct pixz_tar_index_record pixz_tar_index_record;
struct pixz_tar_index_record {
    size_t offset;
    char *name;
    pixz_tar_index_record *next;
};

typedef struct {
    pixz_tar_index_record *first;
    pixz_tar_index_record *last;
} pixz_tar_index;

static pixz_tar_index *pixz_tar_index_new(void);
static void pixz_tar_index_add(pixz_tar_index *i, size_t offset, const char *name);
static void pixz_tar_index_dump(pixz_tar_index *i, FILE *out);
static void pixz_tar_index_free(pixz_tar_index *i);
static int pixz_tar_index_is_metadata(struct archive_entry *entry);


typedef struct {
    FILE *file;
    uint8_t buf[CHUNKSIZE];
} pixz_tar_input;

static int pixz_tar_input_open(struct archive *a, void *refp);
static int pixz_tar_input_close(struct archive *a, void *refp);
static ssize_t pixz_tar_input_read(struct archive *a, void *refp, const void **buf);


int main(void) {
    pixz_tar_index *index = pixz_tar_index_new();
    
    struct archive *a = archive_read_new();
    archive_read_support_compression_none(a);
    archive_read_support_format_tar(a);
    
    pixz_tar_input input = { .file = stdin };    
    if (archive_read_open(a, &input, pixz_tar_input_open, pixz_tar_input_read,
            pixz_tar_input_close) != ARCHIVE_OK)
        pixz_die("Can't open archive\n");
    
    int want_offset = 0;
    size_t offset = 0;
    while (1) {
        struct archive_entry *entry;
        int aerr = archive_read_next_header(a, &entry);
        if (aerr == ARCHIVE_EOF)
            break;
        else if (aerr != ARCHIVE_OK)
            pixz_die("Error reading header\n");
        
        if (!pixz_tar_index_is_metadata(entry)) {
            const char *name = archive_entry_pathname(entry);        
            pixz_tar_index_add(index, offset, name);
            want_offset = 1;
        }
        
        if (archive_read_data_skip(a) != ARCHIVE_OK)
            pixz_die("Error skipping data\n");
        
        if (want_offset) {
            offset = ftell(input.file);
            want_offset = 0;
        }
    }
    if (archive_read_finish(a) != ARCHIVE_OK)
        pixz_die("Error finishing read\n");
    
    pixz_tar_index_dump(index, stdout);
    pixz_tar_index_free(index);
    
    return 0;
}


static int pixz_tar_input_open(struct archive *a, void *refp) {
    return ARCHIVE_OK;
}

static int pixz_tar_input_close(struct archive *a, void *refp) {
    fclose(((pixz_tar_input*)refp)->file);
    return ARCHIVE_OK;
}

static ssize_t pixz_tar_input_read(struct archive *a, void *refp, const void **buf) {
    pixz_tar_input *input = (pixz_tar_input*)refp;
    size_t rd = fread(input->buf, 1, CHUNKSIZE, input->file);
    if (ferror(input->file)) {
        archive_set_error(a, errno, "Read error");
        return -1;
    }
    *buf = input->buf;
    return rd;
}


static pixz_tar_index *pixz_tar_index_new(void) {
    pixz_tar_index *i = malloc(sizeof(pixz_tar_index));
    i->first = NULL;
    i->last = NULL;
    return i;
}

static void pixz_tar_index_add(pixz_tar_index *i, size_t offset, const char *name) {
    pixz_tar_index_record *rec = malloc(sizeof(pixz_tar_index_record));
    rec->next = NULL;
    rec->name = strdup(name);
    rec->offset = offset;
    
    if (!i->first)
        i->first = rec;
    if (i->last)
        i->last->next = rec;
    i->last = rec;
}

static void pixz_tar_index_dump(pixz_tar_index *i, FILE *out) {
    for (pixz_tar_index_record *rec = i->first; rec; rec = rec->next) {
        fprintf(out, "%12zu  %s\n", rec->offset, rec->name);
    }
}

static void pixz_tar_index_free(pixz_tar_index *i) {
    for (pixz_tar_index_record *rec = i->first; rec; rec = rec->next) {
        free(rec->name);
        free(rec);
    }
    free(i);
}

static int pixz_tar_index_is_metadata(struct archive_entry *entry) {
    // FIXME: better copyfile detection?
    
    const char *name = archive_entry_pathname(entry);
    size_t i = strlen(name);
    while (i != 0 && name[i - 1] != '/')
        --i;
    
    return strncmp(name + i, "._", 2) == 0;
}
