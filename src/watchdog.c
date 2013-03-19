/*************************************************************/
/* Original version was an example in the kernel source tree */
/*                                                           */
/* Most of the rest was written by me, Michael Meskes        */
/* meskes@debian.org                                         */
/*                                                           */
/*************************************************************/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <netdb.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/oom.h>
#include <linux/watchdog.h>
#include <string.h>

#include <libgen.h>
#include <dirent.h>

#include <unistd.h>

#include "watch_err.h"
#include "extern.h"

static int no_act = FALSE;
volatile sig_atomic_t _running = 1;

#define ADMIN		"admin"
#define CHANGE		"change"
#define DEVICE		"watchdog-device"
#define DEVICE_TIMEOUT	"watchdog-timeout"
#define	FILENAME	"file"
#define INTERFACE	"interface"
#define INTERVAL	"interval"
#define LOGTICK		"logtick"
#define MAXLOAD1	"max-load-1"
#define MAXLOAD5	"max-load-5"
#define MAXLOAD15	"max-load-15"
#define MAXTEMP		"max-temperature"
#define MINMEM		"min-memory"
#define SERVERPIDFILE	"pidfile"
#define PING		"ping"
#define PINGCOUNT	"ping-count"
#define PRIORITY	"priority"
#define REALTIME	"realtime"
#define REPAIRBIN	"repair-binary"
#define REPAIRTIMEOUT	"repair-timeout"
#define TEMP		"temperature-device"
#define TESTBIN		"test-binary"
#define TESTTIMEOUT	"test-timeout"
#define HEARTBEAT	"heartbeat-file"
#define HBSTAMPS	"heartbeat-stamps"
#define LOGDIR		"log-dir"
#define TESTDIR		"test-directory"

#ifndef TESTBIN_PATH
#define TESTBIN_PATH NULL
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
int maxtemp = 120;
int pingcount = 3;

char *devname = NULL;
char *admin = "root";
char *tempname = NULL;

time_t	test_timeout = 0;				/* test-binary time out value. */
time_t	repair_timeout = 0;				/* repair-binary time out value. */
int		dev_timeout = TIMER_MARGIN;		/* Watchdog harware time-out. */

char *logdir = "/var/log/watchdog";

char *heartbeat = NULL;
int hbstamps = 300;

int realtime = FALSE;

/* Self-repairing binaries list */
struct list *tr_bin_list = NULL;
char *test_dir = TESTBIN_PATH;

struct list *file_list = NULL;
struct list *target_list = NULL;
struct list *pidfile_list = NULL;
struct list *iface_list = NULL;

char *tbinary = NULL;
char *rbinary = NULL;

char *sendmail_bin = PATH_SENDMAIL;

/* Set when write_pid_file() is called. */
pid_t daemon_pid = 0;

/* Command line options also used globally. */
int softboot = FALSE;
int verbose = FALSE;

/* Contineously open file descriptors. */
int mem_fd = -1, temp_fd = -1;
int mlocked = 0;
char *filename_buf;

static void usage(char *progname)
{
	fprintf(stderr, "%s version %d.%d, usage:\n", progname, MAJOR_VERSION, MINOR_VERSION);
	fprintf(stderr, "%s [-F] [-f] [-c <config_file>] [-v] [-s] [-b] [-q]\n", progname);
	exit(1);
}

/* Try to sync */
static int sync_system(int sync_it)
{
	if (sync_it) {
		sync();
		sync();
	}
	return (0);
}

/* execute repair binary */
static int repair(char *rbinary, int result, char *name, int version)
{
	pid_t child_pid;
	pid_t r_pid;
	char parm[5];
	int ret;

	/* no binary given, we have to reboot */
	if (rbinary == NULL)
		return (result);

	sprintf(parm, "%d", result);

	child_pid = fork();
	if (!child_pid) {
		/* Don't want the stdin and stdout of our repair program
		 * to cause trouble.
		 * So make stdout and stderr go to their respective files */
		strcpy(filename_buf, logdir);
		strcat(filename_buf, "/repair-bin.stdout");
		if (!freopen(filename_buf, "a+", stdout))
			exit(errno);
		strcpy(filename_buf, logdir);
		strcat(filename_buf, "/repair-bin.stderr");
		if (!freopen(filename_buf, "a+", stderr))
			exit(errno);

		/* now start binary */
		if (version == 0) {
			if (name == NULL)
				execl(rbinary, rbinary, parm, NULL);
			else
				execl(rbinary, rbinary, parm, name, NULL);
		} else {	/* if (version == 1) */

			if (name == NULL)
				execl(rbinary, rbinary, "repair", parm, NULL);
			else
				execl(rbinary, rbinary, "repair", parm, name, NULL);
		}

		/* execl should only return in case of an error */
		/* so we return the reboot code */
		return (errno);
	} else if (child_pid < 0) {	/* fork failed */
		int err = errno;

		if (errno == EAGAIN) {	/* process table full */
			log_message(LOG_ERR, "process table is full!");
			return (EREBOOT);
		} else if (softboot)
			return (err);
		else
			return (ENOERR);
	}

	if (repair_timeout > 0) {
		time_t left = repair_timeout;
		do {
			sleep(1);
			r_pid = waitpid(child_pid, &result, WNOHANG);
			if (r_pid)
				break;
			left--;
		} while (left > 0);

	} else
		r_pid = waitpid(child_pid, &result, 0);
	if (r_pid == 0) {
		log_message(LOG_ERR, "repair child %d timed out", child_pid);
		return (EREBOOT);
	} else if (r_pid != child_pid) {
		int err = errno;
		log_message(LOG_ERR, "child %d does not exist (errno = %d = '%s')", child_pid, err, strerror(err));
		if (softboot)
			return (err);
	}

	/* check result */
	ret = WEXITSTATUS(result);
	if (ret != 0) {
		log_message(LOG_ERR, "repair binary %s returned %d", rbinary, ret);

		if (ret == ERESET)	/* repair script says force hard reset, we give it a try */
			sleep(dev_timeout * 4);

		/* for all other errors or if we still live, we let shutdown handle it */
		return (ret);
	}

	return (ENOERR);
}

static void wd_action(int result, char *rbinary, char *name, int version)
{
	/* if no-action flag set, do nothing */
	/* no error, keep on working */
	if (result == ENOERR || no_act == TRUE)
		return;

	/* error that might be repairable */
	if (result != EREBOOT)
		result = repair(rbinary, result, name, version);

	/* if still error, reboot */
	if (result != ENOERR)
		do_shutdown(result);

}

static void do_check(int res, char *rbinary, char *name)
{
	wd_action(res, rbinary, name, 0);
	wd_action(keep_alive(), rbinary, NULL, 0);
}

static void do_check2(int res, char *r_specific, char *r_global, char *name)
{
	wd_action(res, r_specific, name, 1);
	wd_action(keep_alive(), r_global, NULL, 0);
}

static void add_list(struct list **list, char *name)
{
	struct list *new, *act;

	new = (struct list *)xcalloc(1, sizeof(struct list));
	new->name = name;
	memset((char *)(&(new->parameter)), '\0', sizeof(union wdog_options));

	if (*list == NULL)
		*list = new;
	else {
		for (act = *list; act->next != NULL; act = act->next) ;
		act->next = new;
	}
}

static int spool(char *line, int *i, int offset)
{
	for ((*i) += offset; line[*i] == ' ' || line[*i] == '\t'; (*i)++) ;
	if (line[*i] == '=')
		(*i)++;
	for (; line[*i] == ' ' || line[*i] == '\t'; (*i)++) ;
	if (line[*i] == '\0')
		return (1);
	else
		return (0);
}

static void read_config(char *configfile)
{
	FILE *wc;
	int gotload5 = FALSE, gotload15 = FALSE;

	if ((wc = fopen(configfile, "r")) == NULL) {
		fatal_error(EX_SYSERR, "Can't open config file \"%s\" (%s)", configfile, strerror(errno));
	}

	while (!feof(wc)) {
		char *line = NULL;
		size_t n;

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
			if (strncmp(line + i, FILENAME, strlen(FILENAME)) == 0) {
				if (spool(line, &i, strlen(FILENAME)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else
					add_list(&file_list, xstrdup(line + i));
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
				if (spool(line, &i, strlen(SERVERPIDFILE)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else
					add_list(&pidfile_list, xstrdup(line + i));
			} else if (strncmp(line + i, PINGCOUNT, strlen(PINGCOUNT)) == 0) {
				if (spool(line, &i, strlen(PINGCOUNT)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else
					pingcount = atol(line + i);
			} else if (strncmp(line + i, PING, strlen(PING)) == 0) {
				if (spool(line, &i, strlen(PING)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else
					add_list(&target_list, xstrdup(line + i));
			} else if (strncmp(line + i, INTERFACE, strlen(INTERFACE)) == 0) {
				if (spool(line, &i, strlen(INTERFACE)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else
					add_list(&iface_list, xstrdup(line + i));
			} else if (strncmp(line + i, REALTIME, strlen(REALTIME)) == 0) {
				(void)spool(line, &i, strlen(REALTIME));
				realtime = (strncmp(line + i, "yes", 3) == 0) ? TRUE : FALSE;
			} else if (strncmp(line + i, PRIORITY, strlen(PRIORITY)) == 0) {
				if (spool(line, &i, strlen(PRIORITY)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else
					schedprio = atol(line + i);
			} else if (strncmp(line + i, REPAIRBIN, strlen(REPAIRBIN)) == 0) {
				if (spool(line, &i, strlen(REPAIRBIN)))
					rbinary = NULL;
				else
					rbinary = xstrdup(line + i);
			} else if (strncmp(line + i, REPAIRTIMEOUT, strlen(REPAIRTIMEOUT)) == 0) {
				if (spool(line, &i, strlen(REPAIRTIMEOUT)))
					repair_timeout = 0;
				else
					repair_timeout = atol(line + i);
			} else if (strncmp(line + i, TESTBIN, strlen(TESTBIN)) == 0) {
				if (spool(line, &i, strlen(TESTBIN)))
					tbinary = NULL;
				else
					tbinary = xstrdup(line + i);
			} else if (strncmp(line + i, TESTTIMEOUT, strlen(TESTTIMEOUT)) == 0) {
				if (spool(line, &i, strlen(TESTTIMEOUT)))
					test_timeout = 0;
				else
					test_timeout = atol(line + i);
			} else if (strncmp(line + i, HEARTBEAT, strlen(HEARTBEAT)) == 0) {
				if (spool(line, &i, strlen(HEARTBEAT)))
					heartbeat = NULL;
				else
					heartbeat = xstrdup(line + i);
			} else if (strncmp(line + i, HBSTAMPS, strlen(HBSTAMPS)) == 0) {
				if (spool(line, &i, strlen(HBSTAMPS)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else
					hbstamps = atol(line + i);
			} else if (strncmp(line + i, ADMIN, strlen(ADMIN)) == 0) {
				if (spool(line, &i, strlen(ADMIN)))
					admin = NULL;
				else
					admin = xstrdup(line + i);
			} else if (strncmp(line + i, INTERVAL, strlen(INTERVAL)) == 0) {
				if (spool(line, &i, strlen(INTERVAL)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else
					tint = atol(line + i);
			} else if (strncmp(line + i, LOGTICK, strlen(LOGTICK)) == 0) {
				if (spool(line, &i, strlen(LOGTICK)))
					logtick = ticker = 1;
				else
					logtick = ticker = atol(line + i);
			} else if (strncmp(line + i, DEVICE, strlen(DEVICE)) == 0) {
				if (spool(line, &i, strlen(DEVICE)))
					devname = NULL;
				else
					devname = xstrdup(line + i);
			} else if (strncmp(line + i, DEVICE_TIMEOUT, strlen(DEVICE_TIMEOUT)) == 0) {
				if (spool(line, &i, strlen(DEVICE_TIMEOUT)))
					fprintf(stderr, "Ignoring invalid line in config file: %s ", line);
				else
					dev_timeout = atol(line + i);
			} else if (strncmp(line + i, TEMP, strlen(TEMP)) == 0) {
				if (spool(line, &i, strlen(TEMP)))
					tempname = NULL;
				else
					tempname = xstrdup(line + i);
			} else if (strncmp(line + i, MAXTEMP, strlen(MAXTEMP)) == 0) {
				if (spool(line, &i, strlen(MAXTEMP)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else
					maxtemp = atol(line + i);
			} else if (strncmp(line + i, MAXLOAD15, strlen(MAXLOAD15)) == 0) {
				if (spool(line, &i, strlen(MAXLOAD15)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else {
					maxload15 = atol(line + i);
					gotload15 = TRUE;
				}
			} else if (strncmp(line + i, MAXLOAD1, strlen(MAXLOAD1)) == 0) {
				if (spool(line, &i, strlen(MAXLOAD1)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else {
					maxload1 = atol(line + i);
					if (!gotload5)
						maxload5 = maxload1 * 3 / 4;
					if (!gotload15)
						maxload15 = maxload1 / 2;
				}
			} else if (strncmp(line + i, MAXLOAD5, strlen(MAXLOAD5)) == 0) {
				if (spool(line, &i, strlen(MAXLOAD5)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else {
					maxload5 = atol(line + i);
					gotload5 = TRUE;
				}
			} else if (strncmp(line + i, MINMEM, strlen(MINMEM)) == 0) {
				if (spool(line, &i, strlen(MINMEM)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else
					minpages = atol(line + i);
			} else if (strncmp(line + i, LOGDIR, strlen(LOGDIR)) == 0) {
				if (spool(line, &i, strlen(LOGDIR)))
					fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
				else
					logdir = xstrdup(line + i);
			} else if (strncmp(line + i, TESTDIR, strlen(TESTDIR)) == 0) {
				if (spool(line, &i, strlen(TESTDIR)))
					fprintf(stderr, "Ignoring invalid line in config file: %s ", line);
				else
					test_dir = xstrdup(line + i);
			} else {
				fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
			}
		}
	}

	if (fclose(wc) != 0) {
		fatal_error(EX_SYSERR, "Error closing file (%s)", strerror(errno));
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
	char *fdup;

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
		if (!(sb.st_mode & S_IXUSR))
			continue;
		if (!(sb.st_mode & S_IRUSR))
			continue;

		fdup = xstrdup(fname);
		if (!fdup)
			continue;

		log_message(LOG_DEBUG, "adding %s to list of auto-repair binaries", fdup);
		add_list(&tr_bin_list, fdup);
	} while (1);
}

static void old_option(int c, char *configfile)
{
	fprintf(stderr, "Option -%c is no longer valid, please specify it in %s.\n", c, configfile);
}

int main(int argc, char *const argv[])
{
	FILE *fp;
	int c, foreground = FALSE, force = FALSE, sync_it = FALSE;
	int hold;
	char *configfile = CONFIG_FILENAME;
	struct list *act;
	pid_t child_pid;
	int oom_adjusted = 0;
	struct stat s;
	char *progname;
	char *opts = "d:i:n:Ffsvbql:p:t:c:r:m:a:";
	struct option long_options[] = {
		{"config-file", required_argument, NULL, 'c'},
		{"foreground", no_argument, NULL, 'F'},
		{"force", no_argument, NULL, 'f'},
		{"sync", no_argument, NULL, 's'},
		{"no-action", no_argument, NULL, 'q'},
		{"verbose", no_argument, NULL, 'v'},
		{"softboot", no_argument, NULL, 'b'},
		{NULL, 0, NULL, 0}
	};
	long count = 0L;

	progname = basename(argv[0]);
	open_logging(progname, MSG_TO_STDERR | MSG_TO_SYSLOG);

	/* check the options */
	/* there aren't that many any more */
	while ((c = getopt_long(argc, argv, opts, long_options, NULL)) != EOF) {
		if (c == -1)
			break;

		switch (c) {
		case 'n':
		case 'p':
		case 'a':
		case 'r':
		case 'd':
		case 't':
		case 'l':
		case 'm':
		case 'i':
			old_option(c, configfile);
			usage(progname);
			break;
		case 'c':
			configfile = optarg;
			break;
		case 'F':
			foreground = TRUE;
			break;
		case 'f':
			force = TRUE;
			break;
		case 's':
			sync_it = TRUE;
			break;
		case 'b':
			softboot = TRUE;
			break;
		case 'q':
			no_act = TRUE;
			break;
		case 'v':
			verbose = TRUE;
			break;
		default:
			usage(progname);
		}
	}

	read_config(configfile);
	add_test_binaries(test_dir);

	if (tint < 0)
		usage(progname);

	if (tint >= dev_timeout && !force) {
		fatal_error(EX_USAGE, "Error:\n"
			"This interval length might reboot the system while the process sleeps!\n"
			"To force this interval length use the -f option.");
	}

	if (maxload1 > 0 && maxload1 < MINLOAD && !force) {
		fatal_error(EX_USAGE, "Error:\n"
			"Using this maximal load average might reboot the system too often!\n"
			"To force this load average use the -f option.");
	}

	/* make sure we get our own log directory */
	if (mkdir(logdir, 0750) && errno != EEXIST) {
		fatal_error(EX_SYSERR, "Cannot create directory %s (%s)", logdir, strerror(errno));
	}

	/* set up pinging if in ping mode */
	if (target_list != NULL) {
		for (act = target_list; act != NULL; act = act->next) {
			struct protoent *proto;
			struct pingmode *net = (struct pingmode *)xcalloc(1, sizeof(struct pingmode));

			/* setup the socket */
			memset(&(net->to), 0, sizeof(struct sockaddr));

			((struct sockaddr_in *)&(net->to))->sin_family = AF_INET;
			if ((((struct sockaddr_in *)&(net->to))->sin_addr.s_addr =
			     inet_addr(act->name)) == (unsigned int)-1) {
			     fatal_error(EX_USAGE, "unknown host %s", act->name);
			}
			net->packet = (unsigned char *)xcalloc((unsigned int)(DATALEN + MAXIPLEN + MAXICMPLEN), sizeof(char));
			if (!(proto = getprotobyname("icmp"))) {
				fatal_error(EX_SYSERR, "unknown protocol icmp.");
			}
			if ((net->sock_fp = socket(AF_INET, SOCK_RAW, proto->p_proto)) < 0
			    || fcntl(net->sock_fp, F_SETFD, 1)) {
			    fatal_error(EX_SYSERR, "error opening socket (%s)", strerror(errno));
			}

			/* this is necessary for broadcast pings to work */
			(void)setsockopt(net->sock_fp, SOL_SOCKET, SO_BROADCAST, (char *)&hold, sizeof(hold));

			hold = 48 * 1024;
			(void)setsockopt(net->sock_fp, SOL_SOCKET, SO_RCVBUF, (char *)&hold, sizeof(hold));

			act->parameter.net = *net;
		}
	}

	/* allocate some memory to store a filename, this is needed later on even
	 * if the system runs out of memory */
	filename_buf = (char *)xcalloc(strlen(logdir) + sizeof("/repair-bin.stdout") + 1, sizeof(char));

#if !defined(DEBUG)
	if (!foreground) {
		/* Become a daemon process: */
		/* make sure we're on the root partition */
		if (chdir("/") < 0) {
			perror(progname);
			exit(1);
		}

		/* fork to go into the background */
		if ((child_pid = fork()) < 0) {
			perror(progname);
			exit(1);
		} else if (child_pid > 0) {
			/* fork was okay          */
			/* wait for child to exit */
			if (waitpid(child_pid, NULL, 0) != child_pid) {
				perror(progname);
				exit(1);
			}
			/* and exit myself */
			exit(0);
		}
		/* and fork again to make sure we inherit all rights from init */
		if ((child_pid = fork()) < 0) {
			perror(progname);
			exit(1);
		} else if (child_pid > 0)
			exit(0);
		/* now we're free */

		/* Okay, we're a daemon     */
		/* but we're still attached to the tty */
		/* create our own session */
		setsid();

		/* As daemon we don't do any console IO */
		close(0);
		close(1);
		close(2);

		open_logging(NULL, MSG_TO_SYSLOG); /* Close terminal output, keep syslog open. */
	}
#endif				/* !DEBUG */

	/* Log the starting message */

	log_message(LOG_INFO, "starting daemon (%d.%d):", MAJOR_VERSION, MINOR_VERSION);
	log_message(LOG_INFO, "int=%ds realtime=%s sync=%s soft=%s mla=%d mem=%d",
	       tint, realtime ? "yes" : "no", sync_it ? "yes" : "no", softboot ? "yes" : "no", maxload1, minpages);

	if (target_list == NULL)
		log_message(LOG_INFO, "ping: no machine to check");
	else
		for (act = target_list; act != NULL; act = act->next)
			log_message(LOG_INFO, "ping: %s", act->name);

	if (file_list == NULL)
		log_message(LOG_INFO, "file: no file to check");
	else
		for (act = file_list; act != NULL; act = act->next)
			log_message(LOG_INFO, "file: %s:%d", act->name, act->parameter.file.mtime);

	if (pidfile_list == NULL)
		log_message(LOG_INFO, "pidfile: no server process to check");
	else
		for (act = pidfile_list; act != NULL; act = act->next)
			log_message(LOG_INFO, "pidfile: %s", act->name);

	if (iface_list == NULL)
		log_message(LOG_INFO, "interface: no interface to check");
	else
		for (act = iface_list; act != NULL; act = act->next)
			log_message(LOG_INFO, "interface: %s", act->name);

	log_message(LOG_INFO, "test=%s(%ld) repair=%s(%ld) alive=%s heartbeat=%s temp=%s to=%s no_act=%s",
	       (tbinary == NULL) ? "none" : tbinary, test_timeout,
	       (rbinary == NULL) ? "none" : rbinary, repair_timeout,
	       (devname == NULL) ? "none" : devname,
	       (heartbeat == NULL) ? "none" : heartbeat,
	       (tempname == NULL) ? "none" : tempname,
	       (admin == NULL) ? "noone" : admin, (no_act == TRUE) ? "yes" : "no");

	/* open the device */
	if (no_act == FALSE) {
		open_watchdog(devname, dev_timeout);
	}

	/* MJ 16/2/2000, need to keep track of the watchdog writes so that
	   I can have a potted history of recent reboots */
	open_heartbeat();

	open_loadcheck();

	if (minpages > 0) {
		/* open the memory info file */
		mem_fd = open("/proc/meminfo", O_RDONLY);
		if (mem_fd == -1) {
			log_message(LOG_ERR, "cannot open /proc/meminfo (errno = %d = '%s')", errno, strerror(errno));
		}
	}

	if (tempname != NULL && no_act == FALSE) {
		/* open the temperature file */
		temp_fd = open(tempname, O_RDONLY);
		if (temp_fd == -1) {
			log_message(LOG_ERR, "cannot open %s (errno = %d = '%s')", tempname, errno, strerror(errno));
		}
	}

	/* tuck my process id away */
	daemon_pid = getpid();
	fp = fopen(PIDFILE, "w");
	if (fp != NULL) {
		fprintf(fp, "%d\n", daemon_pid);
		(void)fclose(fp);
	}

	/* set signal term to set our run flag to 0 so that */
	/* we make sure watchdog device is closed when receiving SIGTERM */
	signal(SIGTERM, sigterm_handler);

#if defined(_POSIX_MEMLOCK)
	if (realtime == TRUE) {
		/* lock all actual and future pages into memory */
		if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
			log_message(LOG_ERR, "cannot lock realtime memory (errno = %d = '%s')", errno, strerror(errno));
		} else {
			struct sched_param sp;

			/* now set the scheduler */
			sp.sched_priority = schedprio;
			if (sched_setscheduler(0, SCHED_RR, &sp) != 0) {
				log_message(LOG_ERR, "cannot set scheduler (errno = %d = '%s')", errno, strerror(errno));
			} else
				mlocked = TRUE;
		}
	}
#endif

	/* tell oom killer to not kill this process */
#ifdef OOM_SCORE_ADJ_MIN
	if (!stat("/proc/self/oom_score_adj", &s)) {
		fp = fopen("/proc/self/oom_score_adj", "w");
		if (fp) {
			fprintf(fp, "%d\n", OOM_SCORE_ADJ_MIN);
			(void)fclose(fp);
			oom_adjusted = 1;
		}
	}
#endif
#ifdef OOM_DISABLE
	if (!oom_adjusted) {
		if (!stat("/proc/self/oom_adj", &s)) {
			fp = fopen("/proc/self/oom_adj", "w");
			if (fp) {
				fprintf(fp, "%d\n", OOM_DISABLE);
				(void)fclose(fp);
				oom_adjusted = 1;
			}
		}
	}
#endif

	if (!oom_adjusted) {
		log_message(LOG_WARNING, "unable to disable oom handling!");
	}

	/* main loop: update after <tint> seconds */
	while (_running) {
		wd_action(keep_alive(), rbinary, NULL, 0);

		/* sync system if we have to */
		do_check(sync_system(sync_it), rbinary, NULL);

		/* check file table */
		do_check(check_file_table(), rbinary, NULL);

		/* check load average */
		do_check(check_load(), rbinary, NULL);

		/* check free memory */
		do_check(check_memory(), rbinary, NULL);

		/* check temperature */
		do_check(check_temp(), rbinary, NULL);

		/* in filemode stat file */
		for (act = file_list; act != NULL; act = act->next)
			do_check(check_file_stat(act), rbinary, act->name);

		/* in pidmode kill -0 processes */
		for (act = pidfile_list; act != NULL; act = act->next)
			do_check(check_pidfile(act), rbinary, act->name);

		/* in network mode check the given devices for input */
		for (act = iface_list; act != NULL; act = act->next)
			do_check(check_iface(act), rbinary, act->name);

		/* in ping mode ping the ip address */
		for (act = target_list; act != NULL; act = act->next)
			do_check(check_net
				 (act->name, act->parameter.net.sock_fp, act->parameter.net.to,
				  act->parameter.net.packet, tint, pingcount), rbinary, act->name);

		/* in user mode execute the given binary or just test fork() call */
		do_check(check_bin(tbinary, test_timeout, 0), rbinary, NULL);

#ifdef TESTBIN_PATH
		/* test/repair binaries in the watchdog.d directory */
		for (act = tr_bin_list; act != NULL; act = act->next)
			/* Use version 1 for testbin-path */
			do_check2(check_bin(act->name, test_timeout, 1), act->name, rbinary, NULL);
#endif

		/* finally sleep some seconds */
		usleep(tint * 500000);	/* this should make watchdog sleep tint seconds alltogther */
		/* sleep(tint); */

		/* do verbose logging */
		if (verbose && logtick && (--ticker == 0)) {
			ticker = logtick;
			count += logtick;
			log_message(LOG_INFO, "still alive after %ld interval(s)", count);
		}
	}

	terminate();
	/* not reached */
	exit(EXIT_SUCCESS);
}
