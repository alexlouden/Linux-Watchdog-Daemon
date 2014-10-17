/* > memory.c
 *
 * Code for periodically checking the 'free' memory in the system. Added in the
 * functions open_memcheck() and close_memcheck() based on stuff from old watchdog.c
 * and shutdown.c to make it more self-contained.
 *
 * TO DO:
 * Should we have separate configuration for checking swap use?
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mman.h>

#include "extern.h"
#include "watch_err.h"

#define FREEMEM		"MemFree:"
#define FREESWAP	"SwapFree:"

static int mem_fd = -1;
static const char mem_name[] = "/proc/meminfo";

/*
 * Open the memory information file if such as test is configured.
 */

int open_memcheck(void)
{
	int rv = -1;

	if (minpages > 0) {
		/* open the memory info file */
		mem_fd = open(mem_name, O_RDONLY);
		if (mem_fd == -1) {
			int err = errno;
			log_message(LOG_ERR, "cannot open %s (errno = %d = '%s')", mem_name, err, strerror(err));
		} else {
			rv = 0;
		}
	}

	return rv;
}

/*
 * Read and check the contents of the memory information file.
 */

int check_memory(void)
{
	char buf[1024], *ptr1, *ptr2;
	unsigned int free;

	/* is the memory file open? */
	if (mem_fd == -1)
		return (ENOERR);

	/* position pointer at start of file */
	if (lseek(mem_fd, 0, SEEK_SET) < 0) {
		int err = errno;
		log_message(LOG_ERR, "lseek %s gave errno = %d = '%s'", mem_name, err, strerror(err));

		if (softboot)
			return (err);

		return (ENOERR);
	}

	/* read the file */
	if (read(mem_fd, buf, sizeof(buf)) < 0) {
		int err = errno;
		log_message(LOG_ERR, "read %s gave errno = %d = '%s'", mem_name, err, strerror(err));

		if (softboot)
			return (err);

		return (ENOERR);
	}

	ptr1 = strstr(buf, FREEMEM);
	ptr2 = strstr(buf, FREESWAP);

	if (!ptr1 || !ptr2) {
		log_message(LOG_ERR, "%s contains invalid data (read = %s)", mem_name, buf);

		if (softboot)
			return (EINVMEM);

		return (ENOERR);
	}

	/* we only care about integer values */
	free = atoi(ptr1 + strlen(FREEMEM)) + atoi(ptr2 + strlen(FREESWAP));

	if (verbose && logtick && ticker == 1)
		log_message(LOG_INFO, "currently there are %d kB of free memory available", free);

	if (free < minpages * (EXEC_PAGESIZE / 1024)) {
		log_message(LOG_ERR, "memory %d kB is less than %d pages", free, minpages);
		return (ENOMEM);
	}

	return (ENOERR);
}

/*
 * Close the special memor data file (if open).
 */

int close_memcheck(void)
{
	int rv = -1;

	if (mem_fd != -1 && close(mem_fd) == -1) {
		log_message(LOG_ALERT, "cannot close %s (errno = %d)", mem_name, errno);
	}

	mem_fd = -1;
	return rv;
}

int check_allocatable(void)
{
	int i;
	char *mem;

	if (minalloc <= 0)
		return 0;

	/*
	 * Map and fault in the pages
	 */
	mem = mmap(NULL, EXEC_PAGESIZE * minalloc, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);
	if (mem == MAP_FAILED) {
		i = errno;
		log_message(LOG_ALERT, "cannot allocate %d bytes (errno = %d)",
			    EXEC_PAGESIZE * minalloc, i);
		return i;
	}

	munmap(mem, EXEC_PAGESIZE * minalloc);
	return 0;
}
