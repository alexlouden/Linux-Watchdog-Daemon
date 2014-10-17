/* > configfile.c
 *
 * Code based on old watchdog.c function to read settings and to get the
 * test binary(s) (if any). Reads the configuration file on a line-by-line
 * basis and parses it for "parameter = value" sort of entries.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "extern.h"
#include "watch_err.h"

static void add_test_binaries(const char *path);

#define ADMIN			"admin"
#define CHANGE			"change"
#define DEVICE			"watchdog-device"
#define DEVICE_TIMEOUT		"watchdog-timeout"
#define	FILENAME		"file"
#define INTERFACE		"interface"
#define INTERVAL		"interval"
#define LOGTICK			"logtick"
#define MAXLOAD1		"max-load-1"
#define MAXLOAD5		"max-load-5"
#define MAXLOAD15		"max-load-15"
#define MAXTEMP			"max-temperature"
#define MINMEM			"min-memory"
#define ALLOCMEM		"allocatable-memory"
#define SERVERPIDFILE		"pidfile"
#define PING			"ping"
#define PINGCOUNT		"ping-count"
#define PRIORITY		"priority"
#define REALTIME		"realtime"
#define REPAIRBIN		"repair-binary"
#define REPAIRTIMEOUT		"repair-timeout"
#define SOFTBOOT		"softboot-option"
#define TEMP			"temperature-device"
#define TEMPPOWEROFF   		"temp-power-off"
#define TESTBIN			"test-binary"
#define TESTTIMEOUT		"test-timeout"
#define HEARTBEAT		"heartbeat-file"
#define HBSTAMPS		"heartbeat-stamps"
#define LOGDIR			"log-dir"
#define TESTDIR			"test-directory"

#ifndef TESTBIN_PATH
#define TESTBIN_PATH 		NULL
#endif

/* Global configuration variables */

int tint = 1;
int logtick = 1;
int ticker = 1;
int schedprio = 1;
int maxload1 = 0;
int maxload5 = 0;
int maxload15 = 0;
int minpages = 0;
int minalloc = 0;
int maxtemp = 120;
int pingcount = 3;
int temp_poweroff = TRUE;

char *devname = NULL;
char *admin = "root";

time_t	test_timeout = 0;				/* test-binary time out value. */
time_t	repair_timeout = 0;				/* repair-binary time out value. */
int	dev_timeout = TIMER_MARGIN;			/* Watchdog hardware time-out. */

char *logdir = "/var/log/watchdog";

char *heartbeat = NULL;
int hbstamps = 300;

int realtime = FALSE;

/* Self-repairing binaries list */
struct list *tr_bin_list = NULL;
static char *test_dir = TESTBIN_PATH;

struct list *file_list = NULL;
struct list *target_list = NULL;
struct list *pidfile_list = NULL;
struct list *iface_list = NULL;
struct list *temp_list = NULL;

char *tbinary = NULL;
char *rbinary = NULL;

/* Command line options also used globally. */
int softboot = FALSE;
int verbose = FALSE;

static void add_list(struct list **list, const char *name)
{
	struct list *new, *act;

	new = (struct list *)xcalloc(1, sizeof(struct list));
	new->name = xstrdup(name);

	if (*list == NULL)
		*list = new;
	else {
		for (act = *list; act->next != NULL; act = act->next) ;
		act->next = new;
	}
}

/* skip from argument to value */
static int spool(char *line, int *i, int offset)
{
	for ((*i) += offset; line[*i] == ' ' || line[*i] == '\t'; (*i)++) ;
	if (line[*i] != '=') {
		/* = sign is missing */
		fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
		return (1);
	}

	(*i)++;
	for (; line[*i] == ' ' || line[*i] == '\t'; (*i)++) ;
	if (line[*i] == '\0') {
		/* no value given */
		fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
		return (1);
	}
	
	return (0);
}

void read_config(char *configfile)
{
	FILE *wc;
	char *line = NULL;
	int gotload5 = FALSE, gotload15 = FALSE;
	size_t n = 0;

	if ((wc = fopen(configfile, "r")) == NULL) {
		fatal_error(EX_SYSERR, "Can't open config file \"%s\" (%s)", configfile, strerror(errno));
	}

	while (!feof(wc)) {

		if (getline(&line, &n, wc) == -1) {
			if (!ferror(wc))
				break;
			else {
				fatal_error(EX_SYSERR, "Error reading config file (%s)", strerror(errno));
			}
		} else {
			int i, j;

			/* scan the actual line for an option */
			/* first remove the leading blanks */
			for (i = 0; line[i] == ' ' || line[i] == '\t'; i++) ;

			/* check for empty line */
			if (line[i] == '\0' || line[i] == '\n')
				continue;

			/* if the next sign is a '#' we have a comment */
			if (line[i] == '#')
				continue;

			/* also remove the trailing blanks and the \n */
			for (j = strlen(line) - 1; line[j] == ' ' || line[j] == '\t' || line[j] == '\n'; j--) ;
			line[j + 1] = '\0';

			/* if the line is empty now, we don't have to parse it */
			if (strlen(line + i) == 0)
				continue;

			/* now check for an option */
			/* order of the comparisons is important to prevent partial matches */
			if (strncmp(line + i, FILENAME, strlen(FILENAME)) == 0) {
				if (!spool(line, &i, strlen(FILENAME)))
					add_list(&file_list, line + i);
			} else if (strncmp(line + i, CHANGE, strlen(CHANGE)) == 0) {
				struct list *ptr;

				if (spool(line, &i, strlen(CHANGE)))
					continue;

				if (!file_list) {	/* no file entered yet */
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
					continue;
				}
				for (ptr = file_list; ptr->next != NULL; ptr = ptr->next) ;
				if (ptr->parameter.file.mtime != 0)
					fprintf(stderr,
						"Duplicate change interval option in config file. Ignoring first entry.\n");

				ptr->parameter.file.mtime = atoi(line + i);
			} else if (strncmp(line + i, SERVERPIDFILE, strlen(SERVERPIDFILE)) == 0) {
				if (!spool(line, &i, strlen(SERVERPIDFILE)))
					add_list(&pidfile_list, line + i);
			} else if (strncmp(line + i, PINGCOUNT, strlen(PINGCOUNT)) == 0) {
				if (!spool(line, &i, strlen(PINGCOUNT)))
					pingcount = atol(line + i);
			} else if (strncmp(line + i, PING, strlen(PING)) == 0) {
				if (!spool(line, &i, strlen(PING)))
					add_list(&target_list, line + i);
			} else if (strncmp(line + i, INTERFACE, strlen(INTERFACE)) == 0) {
				if (!spool(line, &i, strlen(INTERFACE)))
					add_list(&iface_list, line + i);
			} else if (strncmp(line + i, REALTIME, strlen(REALTIME)) == 0) {
				if (!spool(line, &i, strlen(REALTIME)))
					realtime = (strncmp(line + i, "yes", 3) == 0) ? TRUE : FALSE;
			} else if (strncmp(line + i, PRIORITY, strlen(PRIORITY)) == 0) {
				if (!spool(line, &i, strlen(PRIORITY)))
					schedprio = atol(line + i);
			} else if (strncmp(line + i, REPAIRBIN, strlen(REPAIRBIN)) == 0) {
				if (!spool(line, &i, strlen(REPAIRBIN)))
					rbinary = xstrdup(line + i);
			} else if (strncmp(line + i, REPAIRTIMEOUT, strlen(REPAIRTIMEOUT)) == 0) {
				if (!spool(line, &i, strlen(REPAIRTIMEOUT)))
					repair_timeout = atol(line + i);
			} else if (strncmp(line + i, TESTBIN, strlen(TESTBIN)) == 0) {
				if (!spool(line, &i, strlen(TESTBIN)))
					tbinary = xstrdup(line + i);
			} else if (strncmp(line + i, TESTTIMEOUT, strlen(TESTTIMEOUT)) == 0) {
				if (!spool(line, &i, strlen(TESTTIMEOUT)))
					test_timeout = atol(line + i);
			} else if (strncmp(line + i, HEARTBEAT, strlen(HEARTBEAT)) == 0) {
				if (!spool(line, &i, strlen(HEARTBEAT)))
					heartbeat = xstrdup(line + i);
			} else if (strncmp(line + i, HBSTAMPS, strlen(HBSTAMPS)) == 0) {
				if (!spool(line, &i, strlen(HBSTAMPS)))
					hbstamps = atol(line + i);
			} else if (strncmp(line + i, ADMIN, strlen(ADMIN)) == 0) {
				if (!spool(line, &i, strlen(ADMIN)))
					admin = xstrdup(line + i);
			} else if (strncmp(line + i, INTERVAL, strlen(INTERVAL)) == 0) {
				if (!spool(line, &i, strlen(INTERVAL)))
					tint = atol(line + i);
			} else if (strncmp(line + i, LOGTICK, strlen(LOGTICK)) == 0) {
				if (!spool(line, &i, strlen(LOGTICK)))
					logtick = ticker = atol(line + i);
			} else if (strncmp(line + i, DEVICE, strlen(DEVICE)) == 0) {
				if (!spool(line, &i, strlen(DEVICE)))
					devname = xstrdup(line + i);
			} else if (strncmp(line + i, DEVICE_TIMEOUT, strlen(DEVICE_TIMEOUT)) == 0) {
				if (!spool(line, &i, strlen(DEVICE_TIMEOUT)))
					dev_timeout = atol(line + i);
			} else if (strncmp(line + i, TEMP, strlen(TEMP)) == 0) {
				if (!spool(line, &i, strlen(TEMP)))
					add_list(&temp_list, line + i);
			} else if (strncmp(line + i, MAXTEMP, strlen(MAXTEMP)) == 0) {
				if (!spool(line, &i, strlen(MAXTEMP)))
					maxtemp = atol(line + i);
			} else if (strncmp(line + i, MAXLOAD15, strlen(MAXLOAD15)) == 0) {
				if (!spool(line, &i, strlen(MAXLOAD15))) {
					maxload15 = atol(line + i);
					gotload15 = TRUE;
				}
			} else if (strncmp(line + i, MAXLOAD1, strlen(MAXLOAD1)) == 0) {
				if (!spool(line, &i, strlen(MAXLOAD1))) {
					maxload1 = atol(line + i);
					if (!gotload5)
						maxload5 = maxload1 * 3 / 4;
					if (!gotload15)
						maxload15 = maxload1 / 2;
				}
			} else if (strncmp(line + i, MAXLOAD5, strlen(MAXLOAD5)) == 0) {
				if (!spool(line, &i, strlen(MAXLOAD5))) {
					maxload5 = atol(line + i);
					gotload5 = TRUE;
				}
			} else if (strncmp(line + i, MINMEM, strlen(MINMEM)) == 0) {
				if (!spool(line, &i, strlen(MINMEM)))
					minpages = atol(line + i);
			} else if (strncmp(line + i, ALLOCMEM, strlen(ALLOCMEM)) == 0) {
				if (!spool(line, &i, strlen(ALLOCMEM)))
					minalloc = atol(line + i);
			} else if (strncmp(line + i, LOGDIR, strlen(LOGDIR)) == 0) {
				if (!spool(line, &i, strlen(LOGDIR)))
					logdir = xstrdup(line + i);
			} else if (strncmp(line + i, TESTDIR, strlen(TESTDIR)) == 0) {
				if (!spool(line, &i, strlen(TESTDIR)))
					test_dir = xstrdup(line + i);
			} else if (strncmp(line + i, SOFTBOOT, strlen(SOFTBOOT)) == 0) {
				if (!spool(line, &i, strlen(SOFTBOOT)))
					softboot = (strncmp(line + i, "yes", 3) == 0) ? TRUE : FALSE;
			} else if (strncmp(line + i, TEMPPOWEROFF, strlen(TEMPPOWEROFF)) == 0) {
				if (!spool(line, &i, strlen(TEMPPOWEROFF)))
					temp_poweroff = (strncmp(line + i, "yes", 3) == 0) ? TRUE : FALSE;
			} else {
				fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
			}
		}
	}

	if (line)
		free(line);

	if (fclose(wc) != 0) {
		fatal_error(EX_SYSERR, "Error closing file (%s)", strerror(errno));
	}

	add_test_binaries(test_dir);

	if (tint <= 0) {
		fatal_error(EX_SYSERR, "Parameters %s = %d in file \"%s\" must be > 0", INTERVAL, tint, configfile);
	}

}

static void add_test_binaries(const char *path)
{
	DIR *d;
	struct dirent dentry;
	struct dirent *rdret;
	struct stat sb;
	int ret;
	char fname[PATH_MAX];

	if (!path)
		return;
	ret = stat(path, &sb);
	if (ret < 0)
		return;
	if (!S_ISDIR(sb.st_mode))
		return;

	d = opendir(path);
	if (!d)
		return;
	do {
		ret = readdir_r(d, &dentry, &rdret);
		if (ret)
			break;
		if (rdret == NULL)
			break;

		ret = snprintf(fname, sizeof(fname), "%s/%s", path, dentry.d_name);
		if (ret >= sizeof(fname))
			continue;
		ret = stat(fname, &sb);
		if (ret < 0)
			continue;
		if (!S_ISREG(sb.st_mode))
			continue;

		/* Skip any hidden files - a bit suspicious. */
		if(dentry.d_name[0] == '.') {
			log_message(LOG_WARNING, "skipping hidden file %s", fname);
			continue;
		}

		if (!(sb.st_mode & S_IXUSR))
			continue;
		if (!(sb.st_mode & S_IRUSR))
			continue;

		log_message(LOG_DEBUG, "adding %s to list of auto-repair binaries", fname);
		add_list(&tr_bin_list, fname);
	} while (1);

	closedir(d);
}
