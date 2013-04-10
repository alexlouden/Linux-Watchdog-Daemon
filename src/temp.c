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

static int templevel1, have1;
static int templevel2, have2;
static int templevel3, have3;

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

		/*
		 * Clear flags and set/compute warning and max thresholds. Make
		 * sure that each level is distinct and properly orderd so that
		 * we have templevel1 < templevel2 < templevel3 < maxtemp
		 */
		have1 = have2 = have3 = FALSE;

		templevel3 = (maxtemp * 98) / 100;
		if (templevel3 >= maxtemp) {
			templevel3 = maxtemp - 1;
		}

		templevel2 = (maxtemp * 95) / 100;
		if (templevel2 >= templevel3) {
			templevel2 = templevel3 - 1;
		}

		templevel1 = (maxtemp * 90) / 100;
		if (templevel1 >= templevel2) {
			templevel1 = templevel2 - 1;
		}
	}

	return rv;
}

/* ================================================================= */

int check_temp(void)
{
	unsigned char temperature;

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

	/* Print out warnings as we cross the 90/95/98 percent thresholds. */
	if (temperature > templevel3) {
		if (!have3) {
			/* once we reach level3, issue a warning once. */
			log_message(LOG_WARNING, "temperature increases above %d", templevel3);
			have1 = have2 = have3 = TRUE;
		}
	} else if (temperature > templevel2) {
		if (!have2) {
			log_message(LOG_WARNING, "temperature increases above %d", templevel2);
			have1 = have2 = TRUE;
		}
		have3 = FALSE;
	} else if (temperature > templevel1) {
		if (!have1) {
			log_message(LOG_WARNING, "temperature increases above %d", templevel1);
			have1 = TRUE;
		}
		have2 = have3 = FALSE;
	} else {
		/* Below all thresholds, report clear only if previously set. */
		if (have1 || have2 || have3) {
			log_message(LOG_INFO, "temperature now OK again");
		}
		have1 = have2 = have3 = FALSE;
	}

	if (temperature >= maxtemp) {
		log_message(LOG_ERR, "it is too hot inside (temperature = %d >= %d)", temperature, maxtemp);
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
