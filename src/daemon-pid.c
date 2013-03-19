/* > daemon-pid.c
 *
 * Write the PID of the running daemon to its file.
 *
 * (c) 2013 Paul S. Crawford (psc@sat.dundee.ac.uk) under GPL v2 license
 * based on existing code.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "extern.h"

/* Set when write_pid_file() is called. */
pid_t daemon_pid = 0;

/*
 * Code to find the current process' PID and write it to the PID file. This should be
 * called after you daemonize the process (so it gets the correct PID) and before you
 * lock the process memory (as that function needs the PID as well).
 */

int write_pid_file(const char *fname)
{
	FILE *fp;
	int rv = 0;
	daemon_pid = getpid();

	if (fname == NULL)
		return -1;

	fp = fopen(fname, "w");
	if (fp != NULL) {
		fprintf(fp, "%d\n", daemon_pid);
		fclose(fp);
	} else {
		rv = -1;
		log_message(LOG_ERR, "cannot open PID file %s (%s)", fname, strerror(errno));
	}

	return rv;
}

