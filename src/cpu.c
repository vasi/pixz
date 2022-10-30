#define _GNU_SOURCE

#include <unistd.h>

#include "config.h"

#ifdef HAVE_SCHED_GETAFFINITY

#include <sched.h>
#include <stdio.h>

size_t num_threads(void) {
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);

    if (sched_getaffinity(0, sizeof cpu_set, &cpu_set) == -1)
        return sysconf(_SC_NPROCESSORS_ONLN);
    else
        return CPU_COUNT(&cpu_set);
}

#else

size_t num_threads(void) {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

#endif
