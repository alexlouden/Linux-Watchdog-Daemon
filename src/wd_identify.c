/*************************************************************/
/* Small utility to identify hardware watchdog               */
/* 							     */
/* Idea and most of the implementation by		     */
/* Corey Minyard <minyard@acm.org>			     */
/*                                                           */
/* The rest was written by me, Michael Meskes                */
/* meskes@debian.org                                         */
/*                                                           */
/*************************************************************/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <linux/watchdog.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "extern.h"

static void usage(char *progname)
{
	fprintf(stderr, "%s version %d.%d, usage:\n", progname, MAJOR_VERSION, MINOR_VERSION);
	fprintf(stderr, "%s [-c | --config-file <config_file>]\n", progname);
	exit(1);
}

int main(int argc, char *const argv[])
{
	char *configfile = CONFIG_FILENAME;
	int c;
	struct watchdog_info ident;
	char *opts = "c:";
	struct option long_options[] = {
		{"config-file", required_argument, NULL, 'c'},
		{NULL, 0, NULL, 0}
	};
	int watchdog = -1;
	char *progname = basename(argv[0]);

	open_logging(progname, MSG_TO_STDERR);

	/* check for the one option we understand */
	while ((c = getopt_long(argc, argv, opts, long_options, NULL)) != EOF) {
		switch (c) {
		case 'c':
			configfile = optarg;
			break;
		default:
			usage(progname);
		}
	}

	read_config(configfile);

	/* this program has no other function than identifying the hardware behind
	 * this device i.e. if there is no device given we better punt */
	if (devname == NULL) {
		printf("No watchdog hardware configured in \"%s\"\n", configfile);
		exit(0);
	}

	/* open the device */
	watchdog = open(devname, O_WRONLY);
	if (watchdog == -1) {
		log_message(LOG_ERR, "cannot open %s (errno = %d = '%s')", devname, errno, strerror(errno));
		exit(1);
	}

	/* Print watchdog identity */
	if (ioctl(watchdog, WDIOC_GETSUPPORT, &ident) < 0) {
		log_message(LOG_ERR, "cannot get watchdog identity (errno = %d = '%s')", errno, strerror(errno));
	} else {
		ident.identity[sizeof(ident.identity) - 1] = '\0';	/* Be sure */
		printf("%s\n", ident.identity);
	}

	if (write(watchdog, "V", 1) < 0)
		log_message(LOG_ERR, "write watchdog device gave error %d = '%s'!", errno, strerror(errno));

	if (close(watchdog) == -1)
		log_message(LOG_ALERT, "cannot close watchdog (errno = %d = '%s')", errno, strerror(errno));

	close_logging();
	exit(0);
}
