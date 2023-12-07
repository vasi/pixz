#define _GNU_SOURCE

#include "config.h"

#ifdef HAVE_SCHED_GETAFFINITY
#include <sched.h>
#endif

#ifdef HAVE_SYSCONF
#include <unistd.h>
#endif

#ifdef HAVE_GETSYSTEMINFO
#include <sysinfoapi.h>
#endif

size_t num_threads(void) {
#ifdef HAVE_SCHED_GETAFFINITY
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    if (sched_getaffinity(0, sizeof cpu_set, &cpu_set) == 0)
        return CPU_COUNT(&cpu_set);
#endif

#ifdef HAVE_SYSCONF
    return sysconf(_SC_NPROCESSORS_ONLN);
#elif HAVE_GETSYSTEMINFO
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#else
#warning "No processor-detection enabled! Assuming 2 CPUs"
    return 2;
#endif
}
