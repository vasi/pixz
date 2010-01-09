#include "pixz.h"

#include <string.h>

static bool pixz_index_is_prefix(const char *name);
static void pixz_index_add_record(pixz_index *i, size_t offset, const char *name);

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

