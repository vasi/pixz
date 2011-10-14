#include <unistd.h>

#include "pixz.h"

size_t num_threads(size_t max_procs) {
	size_t num_procs = sysconf(_SC_NPROCESSORS_ONLN);
	if (max_procs > num_procs)
		/*num_procs = max_procs;*/

		/* Could just clamp to the max avail, but could also indicate */
		/* user is confused. So err on side of refusing */
		die("Max procs requested was greater than those available.");
    return num_procs;
}
