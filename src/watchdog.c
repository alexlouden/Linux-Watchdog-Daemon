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
char *filename_buf;

static void usage(char *progname)
{
	fprintf(stderr, "%s version %d.%d, usage:\n", progname, MAJOR_VERSION, MINOR_VERSION);
	fprintf(stderr, "%s [options]\n", progname);
	fprintf(stderr, "options:\n");
	fprintf(stderr, "  -c | --config-file <file>  specify location of config file\n");
	fprintf(stderr, "  -f | --force               don't sanity-check config\n");
	fprintf(stderr, "  -F | --foreground          run in foreground\n");
	fprintf(stderr, "  -q | --no-action           do not reboot or halt\n");
	fprintf(stderr, "  -b | --softboot            soft-boot on error\n");
	fprintf(stderr, "  -s | --sync                sync filesystem\n");
	fprintf(stderr, "  -v | --verbose             verbose messages\n");
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

static void old_option(int c, char *configfile)
{
	fprintf(stderr, "Option -%c is no longer valid, please specify it in %s.\n", c, configfile);
}

int main(int argc, char *const argv[])
{
	int c, foreground = FALSE, force = FALSE, sync_it = FALSE;
	char *configfile = CONFIG_FILENAME;
	struct list *act;
	pid_t child_pid;
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
		open_netcheck(target_list);
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

	/* tuck my process id away */
	if (write_pid_file(PIDFILE)) {
		fatal_error(EX_USAGE, "unable to gain lock via PID file");
	}

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

	if (temp_list == NULL)
		log_message(LOG_INFO, "temperature: no sensors to check");
	else {
		log_message(LOG_INFO, "temperature: maximum = %d", maxtemp);
		for (act = temp_list; act != NULL; act = act->next)
			log_message(LOG_INFO, "temperature: %s", act->name);
	}

	log_message(LOG_INFO, "test=%s(%ld) repair=%s(%ld) alive=%s heartbeat=%s to=%s no_act=%s force=%s",
		(tbinary == NULL) ? "none" : tbinary, test_timeout,
		(rbinary == NULL) ? "none" : rbinary, repair_timeout,
		(devname == NULL) ? "none" : devname,
		(heartbeat == NULL) ? "none" : heartbeat,
		(admin == NULL) ? "none" : admin, 
		(no_act == TRUE) ? "yes" : "no",
		(force == TRUE) ? "yes" : "no");

	/* open the device */
	if (no_act == FALSE) {
		open_watchdog(devname, dev_timeout);
		open_tempcheck(temp_list);
	}

	open_heartbeat();

	open_loadcheck();

	open_memcheck();

	/* set signal term to set our run flag to 0 so that */
	/* we make sure watchdog device is closed when receiving SIGTERM */
	signal(SIGTERM, sigterm_handler);

	lock_our_memory(realtime, schedprio, daemon_pid);

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

		/* check allocatable memory */
		do_check(check_allocatable(), rbinary, NULL);

		/* check temperature */
		for (act = temp_list; act != NULL; act = act->next)
			do_check(check_temp(act), rbinary, NULL);

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

		/* finally sleep for a full cycle */
		/* we have just triggered the device with the last check */
		usleep(tint * 1000000);

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
