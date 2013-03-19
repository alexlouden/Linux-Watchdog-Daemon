#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"
#include "watch_err.h"

int check_load(void)
{
	int avg1, avg5, avg15;
	char buf[40], *ptr;

	/* is the load average file open? */
	if (load_fd == -1 || maxload1 == 0 || maxload5 == 0 || maxload15 == 0)
		return (ENOERR);

	/* position pointer at start of file */
	if (lseek(load_fd, 0, SEEK_SET) < 0) {
		int err = errno;
		log_message(LOG_ERR, "lseek /proc/loadavg gave errno = %d = '%s'", err, strerror(err));

		if (softboot)
			return (err);

		return (ENOERR);
	}

	/* read the line (there is only one) */
	if (read(load_fd, buf, sizeof(buf)) < 0) {
		int err = errno;
		log_message(LOG_ERR, "read /proc/loadavg gave errno = %d = '%s'", err, strerror(err));

		if (softboot)
			return (err);

		return (ENOERR);
	}
	/* we only care about integer values */
	avg1 = atoi(buf);

	/* if we have incorrect data we might not be able to find */
	/* the blanks we're looking for */
	ptr = strchr(buf, ' ');
	if (ptr != NULL) {
		avg5 = atoi(ptr);
		ptr = strchr(ptr + 1, ' ');
	}
	if (ptr != NULL)
		avg15 = atoi(ptr);
	else {
		log_message(LOG_ERR, "/proc/loadavg does not contain any data (read = %s)", buf);

		if (softboot)
			return (ENOLOAD);

		return (ENOERR);
	}

	if (verbose && logtick && ticker == 1)
		log_message(LOG_INFO, "current load is %d %d %d", avg1, avg5, avg15);

	if (avg1 > maxload1 || avg5 > maxload5 || avg15 > maxload15) {

		log_message(LOG_ERR, "loadavg %d %d %d is higher than the given threshold %d %d %d!",
							avg1, avg5, avg15,
							maxload1, maxload5, maxload15);

		return (EMAXLOAD);
	}

	return (ENOERR);
}
