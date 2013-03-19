#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <wait.h>

#include "extern.h"
#include "watch_err.h"

int check_pidfile(struct list *file)
{
	int fd = open(file->name, O_RDONLY), pid;
	char buf[10];

	if (fd == -1) {
		int err = errno;
		log_message(LOG_ERR, "cannot open %s (errno = %d = '%s')", file->name, err, strerror(err));

		/* on error ENETDOWN|ENETUNREACH we react as if we're in ping mode 
		 * on ENOENT we assume that the server to be monitored has exited */
		if (softboot || err == ENETDOWN || err == ENETUNREACH || err == ENOENT)
			return (err);

		return (ENOERR);
	}

	/* position pointer at start of file */
	if (lseek(fd, 0, SEEK_SET) < 0) {
		int err = errno;
		log_message(LOG_ERR, "lseek %s gave errno = %d = '%s'", file->name, err, strerror(err));

		close(fd);
		if (softboot)
			return (err);

		return (ENOERR);
	}

	/* just to play it safe */
	memset(buf, 0, sizeof(buf));

	/* read the line (there is only one) */
	if (read(fd, buf, sizeof(buf)) < 0) {
		int err = errno;
		log_message(LOG_ERR, "read %s gave errno = %d = '%s'", file->name, err, strerror(err));

		close(fd);
		if (softboot)
			return (err);

		return (ENOERR);
	}

	/* we only care about integer values */
	pid = atoi(buf);

	if (close(fd) == -1) {
		int err = errno;
		log_message(LOG_ERR, "could not close %s, errno = %d = '%s'", file->name, err, strerror(err));

		if (softboot)
			return (err);

		return (ENOERR);
	}

	if (kill(pid, 0) == -1) {
		int err = errno;
		log_message(LOG_ERR, "pinging process %d (%s) gave errno = %d = '%s'", pid, file->name, err, strerror(err));

		if (softboot || err == ESRCH)
			return (err);

		return (ENOERR);
	}

	/* do verbose logging */
	if (verbose && logtick && ticker == 1)
		log_message(LOG_INFO, "was able to ping process %d (%s).", pid, file->name);

	return (ENOERR);
}
