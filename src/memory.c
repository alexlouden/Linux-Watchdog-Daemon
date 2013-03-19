#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "extern.h"
#include "watch_err.h"

#define FREEMEM		"MemFree:"
#define FREESWAP	"SwapFree:"

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
		log_message(LOG_ERR, "lseek /proc/meminfo gave errno = %d = '%s'", err, strerror(err));

		if (softboot)
			return (err);

		return (ENOERR);
	}

	/* read the file */
	if (read(mem_fd, buf, sizeof(buf)) < 0) {
		int err = errno;
		log_message(LOG_ERR, "read /proc/meminfo gave errno = %d = '%s'", err, strerror(err));

		if (softboot)
			return (err);

		return (ENOERR);
	}

	ptr1 = strstr(buf, FREEMEM);
	ptr2 = strstr(buf, FREESWAP);

	if (!ptr1 || !ptr2) {
		log_message(LOG_ERR, "/proc/meminfo contains invalid data (read = %s)", buf);

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
