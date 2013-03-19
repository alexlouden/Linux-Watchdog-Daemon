#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <unistd.h>

#include "extern.h"
#include "watch_err.h"

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
		log_message(LOG_ERR, "read %s gave errno = %d = '%s'", tempname, err, strerror(err));

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
