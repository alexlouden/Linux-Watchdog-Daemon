/* > keep_alive.c
 *
 * Code here from old keep_alive.c and taken from watchdog.c & shutdown.c to group
 * it together. This has the code to open, refresh, and safely close the watchdog device.
 *
 * While the watchdog daemon can still function without such hardware support, it is
 * MUCH less effective as a result, as it can't deal with kernel faults or very difficult
 * reboot conditions.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stropts.h>			/* for ioctl() */
#include <linux/watchdog.h>		/* for 'struct watchdog_info' */

#include "extern.h"
#include "watch_err.h"

static const int MAX_TIMEOUT = 254;	/* Reasonable limit? Not -1 as char, and probably long enough. */

static int watchdog_fd = -1;

/*
 * Open the watchdog timer (if name non-NULL) and set the time-out value (if non-zero).
 */

int open_watchdog(char *name, int timeout)
{
	struct watchdog_info ident;
	int rv = 0;

	close_watchdog();

	if (name != NULL) {
		watchdog_fd = open(name, O_WRONLY);
		if (watchdog_fd == -1) {
			log_message(LOG_ERR, "cannot open %s (errno = %d = '%s')", name, errno, strerror(errno));
			rv = -1;
			/* do not exit here per default */
			/* we can use watchdog even if there is no watchdog device */
		} else {
			set_watchdog_timeout(timeout);

			/* Also log watchdog identity */
			if (ioctl(watchdog_fd, WDIOC_GETSUPPORT, &ident) < 0) {
				log_message(LOG_ERR, "cannot get watchdog identity (errno = %d = '%s')", errno, strerror(errno));
			} else {
				ident.identity[sizeof(ident.identity) - 1] = '\0';	/* Be sure */
				log_message(LOG_INFO, "hardware watchdog identity: %s", ident.identity);
			}
		}
	}

	return rv;
}

/*
 * Once opened, call this to query or change the watchdog timer value.
 */

int set_watchdog_timeout(int timeout)
{
	int rv = -1;

	if (watchdog_fd != -1) {
		if (timeout > 0) {
			if (timeout > MAX_TIMEOUT)
				timeout = MAX_TIMEOUT;

			/* Set the watchdog hard-stop timeout; default = unset (use driver default) */
			if (ioctl(watchdog_fd, WDIOC_SETTIMEOUT, &timeout) < 0) {
				log_message(LOG_ERR, "cannot set timeout %d (errno = %d = '%s')", timeout, errno, strerror(errno));
			} else {
				log_message(LOG_INFO, "watchdog now set to %d seconds", timeout);
				rv = 0;
			}
		} else {
			timeout = 0;
			/* If called with timeout <= 0 then query device. */
			if (ioctl(watchdog_fd, WDIOC_GETTIMEOUT, &timeout) < 0) {
				log_message(LOG_ERR, "cannot get timeout (errno = %d = '%s')", errno, strerror(errno));
			} else {
				log_message(LOG_INFO, "watchdog was set to %d seconds", timeout);
				rv = 0;
			}
		}
	}

	return rv;
}

/* write to the watchdog device */
int keep_alive(void)
{
	if (watchdog_fd == -1)
		return (ENOERR);

	if (write(watchdog_fd, "\0", 1) < 0) {
		int err = errno;
		log_message(LOG_ERR, "write watchdog device gave error %d = '%s'!", err, strerror(err));

		if (softboot)
			return (err);
	}

	/* MJ 20/2/2001 write a heartbeat to a file outside the syslog, because:
	   - there is no guarantee the system logger is up and running
	   - easier and quicker to parse checkpoint information */
	write_heartbeat();

	return (ENOERR);
}

/*
 * Provide read-only access to the watchdog file handle.
 */

int get_watchdog_fd(void)
{
	return watchdog_fd;
}

/*
 * Close the watchdog device, this normally stops the hardware timer to prevent a
 * spontaneous reboot, but not if the kernel is compiled with the
 * CONFIG_WATCHDOG_NOWAYOUT option enabled!
 */

int close_watchdog(void)
{
	int rv = 0;

	if (watchdog_fd != -1) {
		if (write(watchdog_fd, "V", 1) < 0) {
			int err = errno;
			log_message(LOG_ERR, "write watchdog device gave error %d = '%s'!", err, strerror(err));
			rv = -1;
		}

		if (close(watchdog_fd) == -1) {
			int err = errno;
			log_message(LOG_ALERT, "cannot close watchdog (errno = %d = '%s')", err, strerror(err));
			rv = -1;
		}
	}

	watchdog_fd = -1;

	return rv;
}
