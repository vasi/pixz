#include "pixz.h"
#include <unistd.h>
#include <getopt.h>

typedef enum {
    OP_WRITE,
    OP_READ,
    OP_EXTRACT,
    OP_LIST
} pixz_op_t;

static bool strsuf(char *big, char *small);
static char *subsuf(char *in, char *suf1, char *suf2);
static char *auto_output(pixz_op_t op, char *in);
static void show_help_and_die();


int main(int argc, char **argv) {
    uint32_t level = LZMA_PRESET_DEFAULT;
    bool tar = true;
    pixz_op_t op = OP_WRITE;
    char *ipath = NULL, *opath = NULL;
    size_t max_procs = 0; /* default to however many machine has */

    int ch;
    while ((ch = getopt(argc, argv, "dxli:o:p:tvh0123456789")) != -1) {
        switch (ch) {
            case 'h': show_help_and_die(); break;
            case 'd': op = OP_READ; break;
            case 'x': op = OP_EXTRACT; break;
            case 'l': op = OP_LIST; break;
            case 'i': ipath = optarg; break;
            case 'o': opath = optarg; break;
            case 't': tar = false; break;
            case 'p':
            	max_procs = atoi(optarg);
            	/* will die() if max > avail */
            	num_threads(max_procs);
            	break;
            default:
                if (ch >= '0' && ch <= '9') {
                    level = ch - '0';
                } else {
                    die("Unknown option");
                }
        }
    }
    argc -= optind;
    argv += optind;

    gInFile = stdin;
    gOutFile = stdout;
    bool iremove = false;
    if (op != OP_EXTRACT && argc >= 1) {
        if (argc > 2 || (op == OP_LIST && argc == 2))
            die("Too many arguments");
        if (ipath)
            die("Multiple input files specified");
        ipath = argv[0];

        if (argc == 2) {
            if (opath)
                die("Multiple output files specified");
            opath = argv[1];
        } else if (op != OP_LIST) {
            iremove = true;
            opath = auto_output(op, argv[0]);
        }
    }

    if (ipath && !(gInFile = fopen(ipath, "r")))
        die("Can't open input file");
    if (opath && !(gOutFile = fopen(opath, "w")))
        die("Can't open output file");

    switch (op) {
        case OP_WRITE: pixz_write(tar, level, max_procs); break;
        case OP_READ: pixz_read(tar, 0, NULL, max_procs); break;
        case OP_EXTRACT: pixz_read(tar, argc, argv, max_procs); break;
        case OP_LIST: pixz_list(tar);
    }

    if (iremove)
        unlink(ipath);

    return 0;
}

#define SUF(_op, _s1, _s2) ({ \
    if (op == OP_##_op) { \
        char *r = subsuf(in, _s1, _s2); \
        if (r) \
            return r; \
    } \
})

static char *auto_output(pixz_op_t op, char *in) {
    SUF(READ, ".tar.xz", ".tar");
    SUF(READ, ".tpxz", ".tar");
    SUF(READ, ".xz", "");
    SUF(WRITE, ".tar", ".tpxz");
    SUF(WRITE, "", ".xz");
    die("Unknown suffix");
    return NULL;
}

static bool strsuf(char *big, char *small) {
    size_t bl = strlen(big), sl = strlen(small);
    return strcmp(big + bl - sl, small) == 0;
}

static char *subsuf(char *in, char *suf1, char *suf2) {
    if (!strsuf(in, suf1))
        return NULL;

    size_t li = strlen(in), l1 = strlen(suf1), l2 = strlen(suf2);
    char *r = malloc(li + l2 - l1 + 1);
    memcpy(r, in, li - l1);
    strcpy(r + li - l1, suf2);
    return r;
}

void show_help_and_die(argv)
{
    printf("pixz: parallel xz compression utility\n");
    printf("Usage:\n");
    printf("\n");
    printf("\t-i <filename>: Specify input file (otherwise will read from stdin)\n");
    printf("\t-o <filename>: Specify output file (otherwise will write to stdout)\n");
    printf("\t-d : Decompress (default is to compress in tar mode)\n");
    printf("\t-x : Decompress + extract, interprets input as pixz compressed tar\n");
    printf("\t-l : List contents, interprets input as pixz compressed tar\n");
    printf("\t-t : Do NOT treat data as tar, otherwise defaults to tar interpretation.\n");
    printf("\t-p <N> : Specify maximum number of worker threads.\n");
    printf("\t-0...-9 : Specify compression level.\n");
    printf("\n");

    exit(1);
}
