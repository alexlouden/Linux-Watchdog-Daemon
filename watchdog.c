/*************************************************************/
/* Original version was an example in the kernel source tree */
/*                                                           */
/* Rest was written by me, Michael Meskes                    */
/* meskes@debian.org                                         */
/*                                                           */
/*************************************************************/
#include "version.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/wait.h>

#if defined __GLIBC__
#include <string.h>
#else				/* __GLIBC__ */
extern char *basename(const char *);
#endif				/* __GLIBC__ */

#include <unistd.h>

#include "watch_err.h"
#include "extern.h"

#if defined(USE_SYSLOG)
#include <syslog.h>
#endif				/* USE_SYSLOG */

#if !defined(PIDFILE)
#define PIDFILE "/var/run/watchdog.pid"
#endif				/* !PIDFILE */

#if !defined(DEVNAME)
#define DEVNAME NULL
#endif				/* !DEVNAME */

#if !defined(TEMPNAME)
#define TEMPNAME NULL
#endif				/* !TEMPNAME */

#if !defined(SLEEP_INTERVAL)
#define SLEEP_INTERVAL 10
#endif				/* !SLEEP_INTERVAL */

#if !defined(MAXLOAD)
#define MAXLOAD 12
#endif				/* !MAXLOAD */

#if !defined(MINLOAD)
#define MINLOAD 2
#endif				/* !MINLOAD */

#if !defined(SYSADMIN)
#define SYSADMIN "root"
#endif				/*!SYSADMIN */

#if !defined(CONFIG_FILENAME)
#define CONFIG_FILENAME "/etc/watchdog.conf"
#endif				/* !CONFIG_FILENAME */

#if !defined(CONFIG_LINE_LEN)
#define CONFIG_LINE_LEN 80
#endif				/* !CONFIG_LINE_LEN */


#if !defined(SCHEDULE_PRIORITY)
#define SCHEDULE_PRIORITY 1
#endif				/*!SCHEDULE_PRIORITY */

static int no_act = FALSE;

#if defined(USE_SYSLOG)
int verbose = FALSE;
#endif				/* USE_SYSLOG */

#define ADMIN		"admin"
#define CHANGE		"change"
#define DEVICE		"watchdog-device"
#define	FILENAME	"file"
#define INTERVAL	"interval"
#define MAXLOAD1	"max-load-1"
#define MAXLOAD5	"max-load-5"
#define MAXLOAD15	"max-load-15"
#define CMAXTEMP	"max-temperature"
#define PING		"ping"
#define REPAIRBIN	"repair-binary"
#define TEMP		"temperature-device"
#define TESTBIN		"test-binary"

pid_t pid;
int softboot = FALSE, watchdog = -1, load = -1, temp = -1, tint = SLEEP_INTERVAL;
char *tempname = TEMPNAME, *devname = DEVNAME, *admin = SYSADMIN, *progname;
int maxload1 = MAXLOAD, maxload5 = MAXLOAD * 3 / 4, maxload15 = MAXLOAD / 2;
int maxtemp = MAXTEMP;

#if defined(REALTIME) && defined(_POSIX_MEMLOCK)
int mlocked = FALSE;
#endif

static void usage(void)
{
    fprintf(stderr, "%s version %d.%d, usage:\n", progname, MAJOR_VERSION, MINOR_VERSION);
#if defined(USE_SYSLOG)
    fprintf(stderr, "%s [-i <interval> [-f]] [-l <max load avg>] [-v] [-s] [-b] [-m <max temperature>]\n", progname);
#else				/* USE_SYSLOG */
    fprintf(stderr, "%s [-i <interval> [-f]] [-l <max load avg>] [-v] [-b] [-m <max temperature>]\n", progname);
#endif				/* USE_SYSLOG */
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
static int repair(char *rbinary, int result)
{
    pid_t child_pid;
    char parm[5];
    char ret;

    /* no binary given, we have to reboot */
    if (rbinary == NULL)
	return (result);

    sprintf(parm, "%d", result);

    child_pid = fork();
    if (!child_pid) {
	execl(rbinary, rbinary, parm, NULL);

	/* execl should only return in case of an error */
	/* so we return the reboot code */
	return (errno);
    } else if (child_pid < 0) {	/* fork failed */
	int err = errno;

	if (errno == EAGAIN) {	/* process table full */
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "process table is full!");
#endif				/* USE_SYSLOG */
	    return (EREBOOT);
	} else if (softboot)
	    return (err);
	else
	    return (ENOERR);
    }
    if (waitpid(child_pid, &result, 0) != child_pid) {
	int err = errno;

#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "child %d does not exist (errno = %d)", child_pid, err);
#else				/* USE_SYSLOG */
	perror(progname);
#endif				/* USE_SYSLOG */
	if (softboot)
	    return (err);
    }
    
    /* check result */
    ret = WEXITSTATUS(result);
    if (ret != 0) {
#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "repair binary returned %d", ret);
#endif				/* USE_SYSLOG */

	if (ret == ERESET) /* repair script says force hard reset, we give it a try */
		sleep(TIMER_MARGIN * 4);
	
	/* for all other errors or if we still live, we let shutdown handle it */
	return (ret);
    }
    
    return (ENOERR);
}

static void wd_action(int result, char *rbinary)
{
    /* if no-action flag set, do nothing */
    /* no error, keep on working */
    if (result == ENOERR || no_act == TRUE)
	return;

    /* error that might be repairable */
    if (result != EREBOOT)
	result = repair(rbinary, result);

    /* if still error, reboot */
    if (result != ENOERR)
	do_shutdown(result);

}

static void do_check(int res, char *rbinary)
{
    wd_action(res, rbinary);
    wd_action(keep_alive(), rbinary);
}

struct list *file = NULL, *target = NULL;
char *tbinary, *rbinary, *admin;

static void add_list(struct list **list, char *name)
{
    struct list *new, *act;

    if ((new = (struct list *) calloc(1, sizeof(struct list))) == NULL) {
	fprintf(stderr, "%s: out of memory\n", progname);
	exit(1);
    }
    new->name = name;
    memset((char *) (&(new->parameter)), '\0', sizeof(union options));

    if (*list == NULL)
	*list = new;
    else {
	for (act = *list; act->next != NULL; act = act->next);
	act->next = new;
    }
}

static void spool(char *line, int *i, int offset)
{
    for ((*i) += offset; line[*i] == ' ' || line[*i] == '\t'; (*i)++);
    if (line[*i] == '=')
	(*i)++;
    for (; line[*i] == ' ' || line[*i] == '\t'; (*i)++);
}

static void read_config(char *filename, char *progname)
{
    FILE *wc;
    int gotload5 = FALSE, gotload15 = FALSE;

    if ((wc = fopen(filename, "r")) == NULL) {
	perror(progname);
	exit(1);
    }
    while (!feof(wc)) {
	char line[CONFIG_LINE_LEN];

	if (fgets(line, CONFIG_LINE_LEN, wc) == NULL) {
	    if (!ferror(wc))
		break;
	    else {
		perror(progname);
		exit(1);
	    }
	} else {
	    int i, j;

	    /* scan the actual line for an option */
	    /* first remove the leading blanks */
	    for (i = 0; line[i] == ' ' || line[i] == '\t'; i++);

	    /* if the next sign is a '#' we have a comment */
	    if (line[i] == '#')
		continue;

	    /* also remove the trailing blanks and the \n */
	    for (j = strlen(line) - 1; line[j] == ' ' || line[j] == '\t' || line[j] == '\n'; j--);
	    line[j + 1] = '\0';

	    /* if the line is empty now, we don't have to parse it */
	    if (strlen(line + i) == 0)
		continue;

	    /* now check for an option */
	    if (strncmp(line + i, FILENAME, strlen(FILENAME)) == 0) {
		spool(line, &i, strlen(FILENAME));
		if (strlen(line + i) == 0)
		    fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
		else
		    add_list(&file, strdup(line + i));
	    } else if (strncmp(line + i, CHANGE, strlen(CHANGE)) == 0) {
		struct list *ptr;

		spool(line, &i, strlen(CHANGE));
		if (strlen(line + i) == 0)
		    continue;

		if (!file) {	/* no file entered yet */
		    fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
		    continue;
		}
		for (ptr = file; ptr->next != NULL; ptr = ptr->next);
		if (ptr->parameter.file.mtime != 0)
		    fprintf(stderr, "Duplicate change interval option in config file. Ignoring first entry.\n");

		file->parameter.file.mtime = atoi(line + i);
	    } else if (strncmp(line + i, PING, strlen(PING)) == 0) {
		spool(line, &i, strlen(PING));
		if (strlen(line + i) == 0)
		    fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
		else
		    add_list(&target, strdup(line + i));
	    } else if (strncmp(line + i, REPAIRBIN, strlen(REPAIRBIN)) == 0) {
		spool(line, &i, strlen(REPAIRBIN));
		rbinary = strdup(line + i);
		if (strlen(rbinary) == 0)
		    rbinary = NULL;
	    } else if (strncmp(line + i, TESTBIN, strlen(TESTBIN)) == 0) {
		spool(line, &i, strlen(TESTBIN));
		tbinary = strdup(line + i);
		if (strlen(tbinary) == 0)
		    tbinary = NULL;
	    } else if (strncmp(line + i, ADMIN, strlen(ADMIN)) == 0) {
		spool(line, &i, strlen(ADMIN));
		if (strlen(line + i) == 0)
		    fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
		else
		    admin = strdup(line + i);
	    } else if (strncmp(line + i, INTERVAL, strlen(INTERVAL)) == 0) {
		spool(line, &i, strlen(INTERVAL));
		if (strlen(line + i) == 0)
		    fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
		else
		    tint = atol(strdup(line + i));
	    } else if (strncmp(line + i, DEVICE, strlen(DEVICE)) == 0) {
		spool(line, &i, strlen(DEVICE));
		devname = strdup(line + i);
		if (strlen(devname) == 0)
		    devname = NULL;
	    } else if (strncmp(line + i, TEMP, strlen(TEMP)) == 0) {
		spool(line, &i, strlen(TEMP));
		tempname = strdup(line + i);
		if (strlen(tempname) == 0)
		    tempname = NULL;
	    } else if (strncmp(line + i, CMAXTEMP, strlen(CMAXTEMP)) == 0) {
		spool(line, &i, strlen(CMAXTEMP));
		maxtemp = atol(strdup(line + i));
	    } else if (strncmp(line + i, MAXLOAD1, strlen(MAXLOAD1)) == 0) {
		spool(line, &i, strlen(MAXLOAD1));
		maxload1 = atol(strdup(line + i));
		if (!gotload5)
		    maxload5 = maxload1 * 3 / 4;
		if (!gotload15)
		    maxload15 = maxload1 / 2;
	    } else if (strncmp(line + i, MAXLOAD5, strlen(MAXLOAD5)) == 0) {
		spool(line, &i, strlen(MAXLOAD5));
		maxload5 = atol(strdup(line + i));
		gotload5 = TRUE;
	    } else if (strncmp(line + i, MAXLOAD15, strlen(MAXLOAD15)) == 0) {
		spool(line, &i, strlen(MAXLOAD15));
		maxload15 = atol(strdup(line + i));
		gotload15 = TRUE;
	    } else {
		fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
	    }
	}
    }

    if (fclose(wc) != 0) {
	perror(progname);
	exit(1);
    }
}

static void old_option(int c, char *filename)
{
    fprintf(stderr, "Option -%c is no longer valid, please specify it in %s.\n", c, filename);
    usage();
}

int main(int argc, char *const argv[])
{
    FILE *fp;
    int c, force = FALSE, sync_it = FALSE;
    int hold = 48 * 1024;
    char *filename = CONFIG_FILENAME;
    struct list *act;
    pid_t child_pid;

#if defined(USE_SYSLOG)
    char log[256], *opts = "d:i:n:fsvbql:p:t:c:r:m:a:";
    struct option long_options[] =
    {
	{"config-file", required_argument, NULL, 'c'},
	{"force", no_argument, NULL, 'f'},
	{"sync", no_argument, NULL, 's'},
	{"no-action", no_argument, NULL, 'q'},
	{"verbose", no_argument, NULL, 'v'},
	{"softboot", no_argument, NULL, 'b'}
    };
    long count = 0L;
#else				/* USE_SYSLOG */
    char *opts = "d:i:n:fsbql:p:t:c:r:m:a:";
    struct option long_options[] =
    {
	{"config-file", required_argument, NULL, 'c'},
	{"force", no_argument, NULL, 'f'},
	{"sync", no_argument, NULL, 's'},
	{"no-action", no_argument, NULL, 'q'},
	{"softboot", no_argument, NULL, 'b'}
    };
#endif				/* USE_SYSLOG */

    progname = basename(argv[0]);

    /* check the options */
    /* there arn't that many any more */
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
	    old_option(c, filename);
	    break;
	case 'c':
	    filename = optarg;
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
#if defined(USE_SYSLOG)
	case 'v':
	    verbose = TRUE;
	    break;
#endif				/* USE_SYSLOG */
	default:
	    usage();
	}
    }

    read_config(filename, progname);

    if (tint < 0)
	usage();

    if (tint >= TIMER_MARGIN && !force) {
	fprintf(stderr, "%s error:\n", progname);
	fprintf(stderr, "This interval length might reboot the system while the process sleeps!\n");
	fprintf(stderr, "To force this interval length use the -f option.\n");
	exit(1);
    }
    if (maxload1 < MINLOAD && !force) {
	fprintf(stderr, "%s error:\n", progname);
	fprintf(stderr, "Using this maximal load average might reboot the system to often!\n");
	fprintf(stderr, "To force this load average use the -f option.\n");
	exit(1);
    }
    /* set up pinging if in network mode */
    if (target != NULL) {
	for (act = target; act != NULL; act = act->next) {
	    struct protoent *proto;
	    struct pingmode *net = (struct pingmode *) calloc(1, sizeof(struct pingmode));

	    if (net == NULL) {
		fprintf(stderr, "%s: out of memory\n", progname);
		exit(1);
	    }
	    /* setup the socket */
	    memset(&(net->to), 0, sizeof(struct sockaddr));

	    ((struct sockaddr_in *) &(net->to))->sin_family = AF_INET;
	    if ((((struct sockaddr_in *) &(net->to))->sin_addr.s_addr = inet_addr(act->name)) == (unsigned int) -1) {
		(void) fprintf(stderr, "%s: unknown host %s\n", progname, act->name);
		exit(1);
	    }
	    if (!(net->packet = (unsigned char *) malloc((unsigned int) (DATALEN + MAXIPLEN + MAXICMPLEN)))) {
		fprintf(stderr, "%s: out of memory\n", progname);
		exit(1);
	    }
	    if (!(proto = getprotobyname("icmp"))) {
		(void) fprintf(stderr, "%s: unknown protocol icmp.\n", progname);
		exit(1);
	    }
	    if ((net->sock_fp = socket(AF_INET, SOCK_RAW, proto->p_proto)) < 0) {
		perror(progname);
		exit(1);
	    }
	    (void) setsockopt(net->sock_fp, SOL_SOCKET, SO_RCVBUF, (char *) &hold,
			      sizeof(hold));
	}
    }
    /* make sure we're on the root partition */
    if (chdir("/") < 0) {
	perror(progname);
	exit(1);
    }
#if !defined(DEBUG)
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
#endif				/* !DEBUG */

    /* now we're free */
#if defined(USE_SYSLOG)
#if !defined(DEBUG)
    /* Okay, we're a daemon     */
    /* but we're still attached to the tty */
    /* create our own session */
    setsid();

    /* with USE_SYSLOG we don't do any console IO */
    close(0);
    close(1);
    close(2);
#endif				/* !DEBUG */

    /* Log the starting message */
    openlog(progname, LOG_PID, LOG_DAEMON);
    sprintf(log, "starting daemon (%d.%d):", MAJOR_VERSION, MINOR_VERSION);
    sprintf(log + strlen(log), " int=%ds sync=%s soft=%s mla=%d ping=",
	    tint,
	    sync_it ? "yes" : "no",
	    softboot ? "yes" : "no",
	    maxload1);

    if (target == NULL)
	sprintf(log + strlen(log), "none ");
    else
	for (act = target; act != NULL; act = act->next)
	    sprintf(log + strlen(log), "%s%c", act->name, (act->next != NULL) ? ',' : ' ');

    sprintf(log + strlen(log), "file=");
    if (file == NULL)
	sprintf(log + strlen(log), "none ");
    else
	for (act = file; act != NULL; act = act->next)
	    sprintf(log + strlen(log), "%s:%d%c", act->name, act->parameter.file.mtime, (act->next != NULL) ? ',' : ' ');

    sprintf(log + strlen(log), "test=%s repair=%s alive=%s temp=%s to=%s no_act=%s",
	    (tbinary == NULL) ? "none" : tbinary,
	    (rbinary == NULL) ? "none" : rbinary,
	    (devname == NULL) ? "none" : devname,
	    (tempname == NULL) ? "none" : tempname,
#if defined(SENDTOADMIN)
	    admin,
#else
	    "noone",
#endif				/* SENDTOADMIN */
	    (no_act == TRUE) ? "yes" : "no");
    syslog(LOG_INFO, log);
#endif				/* USE_SYSLOG */


    /* open the device */
    if (devname != NULL && no_act == FALSE) {
	watchdog = open(devname, O_WRONLY);
	if (watchdog == -1) {
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "cannot open %s (errno = %d)", devname, errno);
#else				/* USE_SYSLOG */
	    perror(progname);
#endif				/* USE_SYSLOG */
	    /* do not exit here per default */
	    /* we can use watchdog even if there is no watchdog device */
	}
    }
    /* open the load average file */
    load = open("/proc/loadavg", O_RDONLY);
    if (load == -1) {
#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "cannot open /proc/loadavg (errno = %d)", errno);
#else				/* USE_SYSLOG */
	perror(progname);
#endif				/* USE_SYSLOG */
    }
    if (tempname != NULL && no_act == FALSE) {
	/* open the temperature file */
	temp = open(tempname, O_RDONLY);
	if (temp == -1) {
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "cannot open %s (errno = %d)", tempname, errno);
#else				/* USE_SYSLOG */
	    perror(progname);
#endif				/* USE_SYSLOG */
	}
    }
    /* tuck my process id away */
    fp = fopen(PIDFILE, "w");
    if (fp != NULL) {
	fprintf(fp, "%d\n", pid = getpid());
	(void) fclose(fp);
    }
    /* set signal term to call terminate() */
    /* to make sure watchdog device is closed */
    signal(SIGTERM, terminate);

#if defined(REALTIME) && defined(_POSIX_MEMLOCK)
    /* lock all actual and future pages into memory */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "cannot lock realtime memory (errno = %d)", errno);
#else				/* USE_SYSLOG */
	perror(progname);
#endif
    } else {
	struct sched_param sp;

	/* now set the scheduler */
	sp.sched_priority = SCHEDULE_PRIORITY;
	if (sched_setscheduler(0, SCHED_RR, &sp) != 0) {
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "cannot set scheduler (errno = %d)", errno);
#else				/* USE_SYSLOG */
	    perror(progname);
#endif
	} else
	    mlocked = TRUE;
    }
#endif

    /* main loop: update after <tint> seconds */
    while (1) {
	wd_action(keep_alive(), rbinary);

	/* sync system if we have to */
	do_check(sync_system(sync_it), rbinary);

	/* check file table */
	do_check(check_file_table(), rbinary);

	/* check load average */
	do_check(check_load(), rbinary);

	/* check temperature */
	do_check(check_temp(), rbinary);

	/* in filemode stat file */
	for (act = file; act != NULL; act = act->next)
	    do_check(check_file_stat(act), rbinary);

	/* in network mode ping the ip address */
	for (act = target; act != NULL; act = act->next)
	    do_check(check_net(act->name, act->parameter.net.sock_fp, act->parameter.net.to, act->parameter.net.packet, tint / 3), rbinary);

	/* in user mode execute the given binary or just test fork() call */
	do_check(check_bin(tbinary), rbinary);

	/* finally sleep some seconds */
	sleep(tint);

#if defined(USE_SYSLOG)
	/* do verbose logging */
	if (verbose) {
	    count++;
	    syslog(LOG_INFO, "still alive after %ld seconds = %ld interval(s)", count * tint, count);
	}
#endif				/* USE_SYSLOG */
    }
}
