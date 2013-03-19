#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "extern.h"
#include "watch_err.h"

int check_file_table(void)
{
	int fd;

	/* open a file */
	fd = open("/proc/uptime", O_RDONLY);
	if (fd == -1) {
		int err = errno;

		if (err == ENFILE) {
			/* we need a reboot if ENFILE is returned (file table overflow) */
			log_message(LOG_ERR, "file table overflow detected!");
			return (ENFILE);
		} else {
			errno = err;
			log_message(LOG_ERR, "cannot open /proc/uptime (errno = %d = '%s')", err, strerror(err));

			if (softboot)
				return (err);
		}
	} else {
		if (close(fd) < 0) {
			int err = errno;
			log_message(LOG_ERR, "close /proc/uptime gave errno = %d = '%s'", err, strerror(err));

			if (softboot)
				return (err);
		}
	}

	return (ENOERR);
}
