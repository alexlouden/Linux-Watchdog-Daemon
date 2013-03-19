#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "extern.h"
#include "watch_err.h"

static int temp_fd = -1;

/* ================================================================= */

int open_tempcheck(char *name)
{
	int rv = -1;

	if (name != NULL) {
		/* open the temperature file */
		temp_fd = open(name, O_RDONLY);
		if (temp_fd == -1) {
			log_message(LOG_ERR, "cannot open %s (errno = %d = '%s')", name, errno, strerror(errno));
		} else {
			rv = 0;
		}
	}

	return rv;
}

/* ================================================================= */

int check_temp(void)
{
	unsigned char temperature;
	int templevel1 = maxtemp * 9 / 10, have1 = FALSE;
	int templevel2 = maxtemp * 95 / 100, have2 = FALSE;
	int templevel3 = maxtemp * 98 / 100, have3 = FALSE;

	/* is the temperature device open? */
	if (temp_fd == -1)
		return (ENOERR);

	/* read the line (there is only one) */
	if (read(temp_fd, &temperature, sizeof(temperature)) < 0) {
		int err = errno;
		log_message(LOG_ERR, "read temperature gave errno = %d = '%s'", err, strerror(err));

		if (softboot)
			return (err);

		return (ENOERR);
	}

	if (verbose && logtick && ticker == 1)
		log_message(LOG_INFO, "current temperature is %d", temperature);

	if (temperature > templevel3) {
		if (!have3) {
			/* once we reach level3, issue a warning */
			log_message(LOG_WARNING, "temperature increases %d", templevel3);
			have1 = have2 = have3 = TRUE;
		}
	} else {
		have3 = FALSE;
		if (temperature > templevel2) {
			if (!have2) {
				/* once we reach level2, issue a warning */
				log_message(LOG_WARNING, "temperature increases %d", templevel2);
				have1 = have2 = TRUE;
			}
		} else {
			have2 = have3 = FALSE;
			if (temperature > templevel1) {
				if (!have1) {
					/* once we reach level1, issue a warning */
					log_message(LOG_WARNING, "temperature increases %d", templevel1);
					have1 = TRUE;
				}
			}
		}
	}

	if (temperature >= maxtemp) {
		log_message(LOG_ERR, "it is too hot inside (temperature = %d)", temperature);
		return (ETOOHOT);
	}
	return (ENOERR);
}

/* ================================================================= */

int close_tempcheck(void)
{
	int rv = -1;

	if (temp_fd != -1 && close(temp_fd) == -1) {
		log_message(LOG_ALERT, "cannot close temperature device (errno = %d)", errno);
	} else {
		rv = 0;
	}

	temp_fd = -1;
	return rv;
}
