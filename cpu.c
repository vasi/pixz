#include <unistd.h>

size_t num_threads(void) {
    return sysconf(_SC_NPROCESSORS_ONLN);
}
