/*************************************************************/
/* Original version was an example in the kernel source tree */
/*                                                           */
/* Rest was written by me, Michael Meskes                    */
/* meskes@topsystem.de                                       */
/*                                                           */
/*************************************************************/
#include "version.h"

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "watch_err.h"
#include <stdlib.h>
#include <strings.h>
#include <paths.h>
#include <utmp.h>
#include <mntent.h>
#include <dirent.h>
#include <setjmp.h>

#if defined __GLIBC__
#include "glibc_compat.h"
#include <sys/quota.h>
#include <sys/swap.h>
#include <sys/reboot.h>
#include <string.h>
#else				/* __GLIBC__ */
#include <linux/quota.h>
extern char *basename(const char *);
#endif				/* __GLIBC__ */

#include <unistd.h>

#if defined(USE_SYSLOG)
#include <syslog.h>
#endif				/* USE_SYSLOG */

#if !defined(PIDFILE)
#define PIDFILE "/var/run/watchdog.pid"
#endif				/* !PIDFILE */

#if !defined(RANDOM_SEED)
#define RANDOM_SEED "/var/run/random-seed"
#endif				/* !RANDOM_SEED */

#if !defined(DEVNAME)
#define DEVNAME NULL
#endif				/* !DEVNAME */

#if !defined(TEMPNAME)
#define TEMPNAME NULL
#endif				/* !TEMPNAME */

#if !defined(TIMER_MARGIN)
#define TIMER_MARGIN 60
#endif				/* !TIMER_MARGIN */

#if !defined(SLEEP_INTERVAL)
#define SLEEP_INTERVAL 10
#endif				/* !SLEEP_INTERVAL */

#if !defined(MAXTEMP)
#define MAXTEMP 120
#endif				/* !MAXTEMP */

#if !defined(MAXLOAD)
#define MAXLOAD 12
#endif				/* !MAXLOAD */

#if !defined(PATH_SENDMAIL)
#define PATH_SENDMAIL _PATH_SENDMAIL
#endif				/* PATH_SENDMAIL */

#if !defined(SYSADMIN)
#define SYSADMIN "root"
#endif				/*!SYSADMIN */

#define TRUE 1
#define FALSE 0

#define DATALEN         (64 - 8)
#define MAXIPLEN        60
#define MAXICMPLEN      76
#define MAXPACKET       (65536 - 60 - 8)	/* max packet size */

extern void umount_all(void *);
extern int ifdown(void);
extern int mount_one(char *, char *, char *, char *, int, int);

static int watchdog = -1, softboot = FALSE, load = -1, temp = -1;
pid_t pid;
static char *devname = DEVNAME, *tempname = TEMPNAME, *admin = SYSADMIN;
char *progname;
static int maxload1 = MAXLOAD, maxload5 = MAXLOAD * 3 / 4, maxload15 = MAXLOAD / 2;
static int maxtemp = MAXTEMP;
static struct mntent rootfs;

#if defined(USE_SYSLOG)
static int templevel1 = MAXTEMP * 9 / 10, have1 = FALSE;
static int templevel2 = MAXTEMP * 95 / 100, have2 = FALSE;
static int templevel3 = MAXTEMP * 98 / 100, have3 = FALSE;
static int verbose = FALSE;
#endif				/* USE_SYSLOG */

jmp_buf ret2dog;

/* Info about a process. */
typedef struct _proc_ {
    pid_t pid;			/* Process ID.                    */
    int sid;			/* Session ID.                    */
    struct _proc_ *next;	/* Pointer to next struct.        */
} PROC;

/* write a log entry on exit */
static void log_end()
{
#if defined(USE_SYSLOG)
    /* Log the closinging message */
    syslog(LOG_INFO, "stopping daemon (%d.%d)", MAJOR_VERSION, MINOR_VERSION);
    closelog();

    sleep(5);			/* make sure log is written */
#endif				/* USE_SYSLOG */
    return;
}

/* close the device and check for error */
static void close_all()
{
    if (watchdog != -1 && close(watchdog) == -1) {
#if defined(USE_SYSLOG)
	syslog(LOG_ALERT, "cannot close %s", devname);
#else				/* USE_SYSLOG */
	perror(progname);
#endif				/* USE_SYSLOG */
    }
    if (load != -1 && close(load) == -1) {
#if defined(USE_SYSLOG)
	syslog(LOG_ALERT, "cannot close /proc/loadavg");
#else				/* USE_SYSLOG */
	perror(progname);
#endif				/* USE_SYSLOG */
    }
    if (temp != -1 && close(temp) == -1) {
#if defined(USE_SYSLOG)
	syslog(LOG_ALERT, "cannot close /dev/temperature");
#else				/* USE_SYSLOG */
	perror(progname);
#endif				/* USE_SYSLOG */
    }
}

/* on exit we close the device and log that we stop */
static void terminate(int arg)
{
    close_all();
    log_end();
    exit(0);
}

static void usage(void)
{
    fprintf(stderr, "%s version %d.%d, usage:\n", progname, MAJOR_VERSION, MINOR_VERSION);
#if defined(USE_SYSLOG)
    fprintf(stderr, "%s [-i <interval> [-f]] [-n <file-name>] [-l <max load avg>] [-p <ipaddr> ] [-v] [-s]  [-b]  [-d <watchdog-device>]  [-t <temperature-device>] [-m <max temperature>] [-c <check-binary> ] [-r <repair-binary>] [-a <admin>]\n", progname);
#else				/* USE_SYSLOG */
    fprintf(stderr, "%s [-i <interval> [-f]] [-n <file-name>] [-l <max load avg>] [-p <ipaddr> ] [-v] [-b]  [-d <watchdog-device>]  [-t <temperature-device>] [-m <max temperature>] [-c <check-binary> ] [-r <repair-binary>] [-a <admin>]\n", progname);
#endif				/* USE_SYSLOG */
    exit(1);
}

/* panic: we're still alive but shouldn't */
static void panic(void)
{
    /* if we are still alive, we just exit */
    close_all();
    fprintf(stderr, "WATCHDOG PANIC: Still alive after sleeping %d seconds!\n", 4 * TIMER_MARGIN);
#if defined(USE_SYSLOG)
    openlog(progname, LOG_PID, LOG_DAEMON);
    syslog(LOG_ALERT, "still alive after sleeping %d seconds", 4 * TIMER_MARGIN);
    closelog();
#endif
    exit(1);
}

static void mnt_off()
{
    FILE *fp;
    struct mntent *mnt;

    fp = setmntent(MNTTAB, "r");
    while ((mnt = getmntent(fp)) != (struct mntent *) 0) {
	/* First check if swap */
	if (!strcmp(mnt->mnt_type, MNTTYPE_SWAP))
	    if (swapoff(mnt->mnt_fsname) < 0)
		perror(mnt->mnt_fsname);

	/* quota only if mounted at boot time && filesytem=ext2 */
	if (hasmntopt(mnt, MNTOPT_NOAUTO) || strcmp(mnt->mnt_type, MNTTYPE_EXT2))
	    continue;

	/* group quota? */
	if (hasmntopt(mnt, MNTOPT_GRPQUOTA))
	    if (quotactl(QCMD(Q_QUOTAOFF, GRPQUOTA), mnt->mnt_fsname, 0, (caddr_t) 0) < 0)
		perror(mnt->mnt_fsname);

	/* user quota */
	if (hasmntopt(mnt, MNTOPT_USRQUOTA))
	    if (quotactl(QCMD(Q_QUOTAOFF, USRQUOTA), mnt->mnt_fsname, 0, (caddr_t) 0) < 0)
		perror(mnt->mnt_fsname);

	/* save entry if root partition */
	rootfs.mnt_freq = mnt->mnt_freq;
	rootfs.mnt_passno = mnt->mnt_passno;

	rootfs.mnt_fsname = strdup(mnt->mnt_fsname);
	rootfs.mnt_dir = strdup(mnt->mnt_dir);
	rootfs.mnt_type = strdup(mnt->mnt_type);

	/* did we get enough memory? */
	if (rootfs.mnt_fsname == NULL || rootfs.mnt_dir == NULL || rootfs.mnt_type == NULL) {
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "out of memory");
#else				/* USE_SYSLOG */
	    fprintf(stderr, "%s: out of memory\n", progname);
#endif
	}
	/* while we´re at it we add the remount option */
	if (strcmp(mnt->mnt_dir, "/") == 0) {
	    if ((rootfs.mnt_opts = malloc(strlen(mnt->mnt_opts) + strlen("remount,ro") + 2)) == NULL) {
#if defined(USE_SYSLOG)
		syslog(LOG_ERR, "out of memory");
#else				/* USE_SYSLOG */
		fprintf(stderr, "%s: out of memory\n", progname);
#endif
	    } else
		sprintf(rootfs.mnt_opts, "%s,remount,ro", mnt->mnt_opts);
	}
    }
    endmntent(fp);
}

/* Parts of the following two functions are taken from Miquel van */
/* Smoorenburg's killall5 program. */

static PROC *plist;

/* get a list of all processes */
static int readproc()
{
    DIR *dir;
    struct dirent *d;
    pid_t act_pid;
    PROC *p;

    /* Open the /proc directory. */
    if ((dir = opendir("/proc")) == NULL) {
#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "cannot opendir /proc");
#else				/* USE_SYSLOG */
	perror(progname);
#endif
	return (-1);
    }
    plist = NULL;

    /* Walk through the directory. */
    while ((d = readdir(dir)) != NULL) {

	/* See if this is a process */
	if ((act_pid = atoi(d->d_name)) == 0)
	    continue;

	/* Get a PROC struct . */
	if ((p = (PROC *) calloc(1, sizeof(PROC))) == NULL) {
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "out of memory");
#else				/* USE_SYSLOG */
	    fprintf(stderr, "%s: out of memory\n", progname);
#endif
	    return (-1);
	}
	p->sid = getsid(act_pid);
	p->pid = act_pid;

	/* Link it into the list. */
	p->next = plist;
	plist = p;
    }
    closedir(dir);

    /* Done. */
    return (0);
}

static void killall5(int sig)
{
    PROC *p;
    int sid = -1;

    /*
     *    Ignoring SIGKILL and SIGSTOP do not make sense, but
     *    someday kill(-1, sig) might kill ourself if we don't
     *    do this. This certainly is a valid concern for SIGTERM-
     *    Linux 2.1 might send the calling process the signal too.
     */

    /* Since we ignore all signals, we don't have to worry here. MM */
    /* Now stop all processes. */
    kill(-1, SIGSTOP);

    /* Find out our own 'sid'. */
    if (readproc() < 0) {
	kill(-1, SIGCONT);
	return;
    }
    for (p = plist; p; p = p->next)
	if (p->pid == pid) {
	    sid = p->sid;
	    break;
	}
    /* Now kill all processes except our session. */
    for (p = plist; p; p = p->next)
	if (p->pid != pid && p->sid != sid)
	    kill(p->pid, sig);

    /* And let them continue. */
    kill(-1, SIGCONT);
}

/* shut down the system */
static void do_shutdown(int errorcode)
{
    int i = 0, fd;
    char *seedbck = RANDOM_SEED;

    /* soft-boot the system */
    /* first close the open files */
    close_all();

#if defined(USE_SYSLOG)
    /* now tell syslog what's happening */
    syslog(LOG_ALERT, "shutting down the system because of error %d", errorcode);
    closelog();
#endif				/* USE_SYSLOG */

#if defined(SENDTOADMIN)
    /* if we will halt the system we should try to tell a sysadmin */
    if (errorcode == ETOOHOT) {
	/* send mail to the system admin */
	FILE *ph;
	char exe[128];

	sprintf(exe, "%s -i %s", PATH_SENDMAIL, admin);
	ph = popen(exe, "w");
	if (ph == NULL) {
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "cannot start %s (errno = %d)", PATH_SENDMAIL, errno);
#endif				/* USE_SYSLOG */
	} else {
	    char myname[MAXHOSTNAMELEN + 1];
	    struct hostent *hp;

	    /* get my name */
	    gethostname(myname, sizeof(myname));

	    fprintf(ph, "To: %s\n", admin);
	    if (ferror(ph) != 0) {
#if defined(USE_SYSLOG)
		syslog(LOG_ERR, "cannot send mail (errno = %d)", errno);
#endif				/* USE_SYSLOG */
	    }
	    
	    /* if possible use the full name including domain */
	    if ((hp = gethostbyname(myname)) != NULL)
		fprintf(ph, "Subject: %s is going down!\n\n", hp->h_name);
	    else
		fprintf(ph, "Subject: %s is going down!\n\n", myname);
	    if (ferror(ph) != 0) {
#if defined(USE_SYSLOG)
		syslog(LOG_ERR, "cannot send mail (errno = %d)", errno);
#endif				/* USE_SYSLOG */
	    }
	    fprintf(ph, "It is too hot to keep on working. The system will be halted!\n");
	    if (ferror(ph) != 0) {
#if defined(USE_SYSLOG)
		syslog(LOG_ERR, "cannot send mail (errno = %d)", errno);
#endif				/* USE_SYSLOG */
	    }
	    if (pclose(ph) == -1) {
#if defined(USE_SYSLOG)
		syslog(LOG_ERR, "cannot finish mail (errno = %d)", errno);
#endif				/* USE_SYSLOG */
	    }
	    /* finally give the system a little bit of time to deliver */
	}
    }
#endif				/* SENDTOADMIN */

    sleep(10);			/* make sure log is written and mail is send */

    /* We cannot start shutdown, since init might not be able to fork. */
    /* That would stop the reboot process. So we try rebooting the system */
    /* ourselves. Note, that it is very likely we cannot start any rc */
    /* script either, so we do it all here. */

    /* Start with closing the files. */
    for (i = 0; i < 3; i++)
	if (!isatty(i))
	    close(i);
    for (i = 3; i < 20; i++)
	close(i);
    close(255);

    /* Ignore all signals. */
    for (i = 1; i < _NSIG; i++)
	signal(i, SIG_IGN);
	
    /* Stop init; it is insensitive to the signals sent by the kernel. */
    kill(1, SIGTSTP);

    /* Kill all processes. */
    (void) killall5(SIGTERM);
    sleep(5);
    (void) killall5(SIGKILL);

    /* Record the fact that we're going down */
    if ((fd = open(_PATH_WTMP, O_WRONLY | O_APPEND)) >= 0) {
	time_t t;
	struct utmp wtmp;

	time(&t);
	strcpy(wtmp.ut_user, "shutdown");
	strcpy(wtmp.ut_line, "~");
	strcpy(wtmp.ut_id, "~~");
	wtmp.ut_pid = 0;
	wtmp.ut_type = RUN_LVL;
	wtmp.ut_time = t;
	write(fd, (char *) &wtmp, sizeof(wtmp));
	close(fd);
    }
    
    /* save the random seed if a save location exists */
    /* don't worry about error messages, we react here anyway */

    if (strlen(seedbck) != 0) {
	int fd_seed;

	if ((fd_seed = open("/dev/urandom", O_RDONLY)) >= 0) {
	    int fd_bck;

	    if ((fd_bck = creat(seedbck, S_IRUSR | S_IWUSR)) >= 0) {
		char buf[512];

		if (read(fd_seed, buf, 512) == 512)
		    write(fd_bck, buf, 512);
		close(fd_bck);
	    }
	    close(fd_seed);
	}
    }

    /* Turn off accounting */
    if (acct(NULL) < 0)
	perror(progname);

    /* Turn off quota and swap */
    mnt_off();

    /* umount all partitions */
    if (setjmp(ret2dog) == 0)
	umount_all(NULL);

    /* remount / read-only */
    if (setjmp(ret2dog) == 0)
	mount_one(rootfs.mnt_fsname, rootfs.mnt_dir, rootfs.mnt_type,
		  rootfs.mnt_opts, rootfs.mnt_freq, rootfs.mnt_passno);

    /* shut down interfaces (also taken from sysvinit source */
    ifdown();

    /* finally reboot */
    if (errorcode != ETOOHOT) {
#ifdef __GLIBC__
	reboot(RB_AUTOBOOT);
#else				/* __GLIBC__ */
	reboot(0xfee1dead, 672274793, 0x01234567);
#endif				/* __GLIBC__ */
    } else {
	/* rebooting makes no sense if it's too hot */
	/* Turn on hard reboot, CTRL-ALT-DEL will reboot now */
#ifdef __GLIBC__
	reboot(RB_ENABLE_CAD);
#else				/* __GLIBC__ */
	reboot(0xfee1dead, 672274793, 0x89abcdef);
#endif				/* __GLIBC__ */

	/* And perform the `halt' system call. */
#ifdef __GLIBC__
	reboot(RB_HALT_SYSTEM);
#else				/* __GLIBC__ */
	reboot(0xfee1dead, 672274793, 0xcdef0123);
#endif
    }

    /* okay we should never reach this point, */
    /* but if we do we will cause the hard reset */

    /* open the device again */
    /* don't check for error, it won't help anyway here */
    if (devname != NULL) {
	open(devname, O_WRONLY);

	sleep(TIMER_MARGIN * 4);
    }
    /* unbelievable: we're still alive */
    panic();

}

/* Try to sync */
static void sync_system(void)
{
    sync();
    sync();
}

/* check if process table is full */
static int check_fork(void)
{

    pid_t child_pid;

    child_pid = fork();
    if (!child_pid)
	exit(0);		/* child, exit immediately */
    else if (child_pid < 0) {	/* fork failed */
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
    } else {
	/* fork was okay          */
	/* wait for child to stop */
	if (waitpid(child_pid, NULL, 0) != child_pid) {
	    int err = errno;

#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "child %d does not exist (error = %d)", child_pid, err);
#else				/* USE_SYSLOG */
	    perror(progname);
#endif				/* USE_SYSLOG */
	    if (softboot)
		return (err);
	}
    }
    return (ENOERR);
}

static int check_file_stat(char *filename)
{
    struct stat buf;

    /* in filemode stat file */
    if (filename == NULL)
	return (ENOERR);

    if (stat(filename, &buf) == -1) {
	int err = errno;

#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "cannot stat %s (errno = %d)", filename, err);
#else				/* USE_SYSLOG */
	perror(progname);
#endif				/* USE_SYSLOG */
	/* on error 100|101 we react as if we're in network mode */
	if (softboot || err == 100 || err == 101)
	    return (err);
    }
    return (ENOERR);
}

static int check_load()
{
    int avg1, avg5, avg15;
    char buf[40], *ptr;

    /* is the load average file open? */
    if (load == -1)
	return (ENOERR);

    /* position pointer at start of file */
    if (lseek(load, 0, SEEK_SET) < 0) {
	int err = errno;

#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "lseek /proc/loadavg gave errno = %d", err);
#else				/* USE_SYSLOG */
	perror(progname);
#endif				/* USE_SYSLOG */
	if (softboot)
	    return (err);

	return (ENOERR);
    }
    /* read the line (there is only one) */ if (read(load, buf, sizeof(buf)) < 0) {
	int err = errno;

#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "read /proc/loadavg gave errno = %d", err);
#else				/* USE_SYSLOG */
	perror(progname);
#endif				/* USE_SYSLOG */
	if (softboot)
	    return (err);

	return (ENOERR);
    }
    /* we only care about integer values */
    avg1 = atoi(buf);

    /* if we have incorrect data we might not be able to find */
    /* the blanks we're looking for */
    ptr = strchr(buf, ' ');
    if (ptr != NULL) {
	avg5 = atoi(ptr);
	ptr = strchr(ptr + 1, ' ');
    }
    if (ptr != NULL)
	avg15 = atoi(ptr);
    else {
#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "/proc/loadavg does not contain any data (read = %s)", buf);
#else				/* USE_SYSLOG */
	perror(progname);
#endif				/* USE_SYSLOG */
	if (softboot)
	    return (ENOLOAD);

	return (ENOERR);
    }

#if defined(USE_SYSLOG)
    if (verbose)
	syslog(LOG_INFO, "current load is %d %d %d", avg1, avg5, avg15);
#endif				/* USE_SYSLOG */

    if (avg1 > maxload1 || avg5 > maxload5 || avg15 > maxload15) {
#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "loadavg %d %d %d > %d %d %d!", avg1, avg5, avg15,
	       maxload1, maxload5, maxload15);
#endif				/* USE_SYSLOG */
	return (EMAXLOAD);
    }
    return (ENOERR);
}

/* write to the watchdog device */
static int keep_alive(void)
{
    if (watchdog == -1)
	return (ENOERR);

    if (write(watchdog, "\0", 1) < 0) {
	int err = errno;

#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "write watchdog device gave error %d!", err);
#endif
	if (softboot)
	    return (err);
    }
    return (ENOERR);
}

/*
 * in_cksum --
 *      Checksum routine for Internet Protocol family headers (C Version)
 */
static int in_cksum(unsigned short *addr, int len)
{
    int nleft = len, sum = 0;
    unsigned short *w = addr, answer = 0;

    /*
       * Our algorithm is simple, using a 32 bit accumulator (sum), we add
       * sequential 16 bit words to it, and at the end, fold back all the
       * carry bits from the top 16 bits into the lower 16 bits.
     */
    while (nleft > 1) {
	sum += *w++;
	nleft -= 2;
    }				/* mop up an odd byte, if necessary */
    if (nleft == 1) {
	*(unsigned char *) (&answer) = *(unsigned char *) w;
	sum += answer;
    }
    /* add back carry outs from top 16 bits to low 16 bits */
    sum = (sum >> 16) + (sum & 0xffff);		/* add hi 16 to low 16 */
    sum += (sum >> 16);		/* add carry */
    answer = ~sum;		/* truncate to 16 bits */
    return (answer);
}

static int check_net(char *target, int sock_fp, struct sockaddr to, unsigned char *packet, int time)
{
    int i;
    unsigned char outpack[MAXPACKET];

    if (target == NULL)
	return (ENOERR);

    /* try at most three times */
    for (i = 0; i < 3; i++) {

	struct sockaddr_in from;
	int fromlen, fdmask, j;
	struct timeval timeout;
	struct icmphdr *icp = (struct icmphdr *) outpack;

	/* setup a ping message */
	icp->type = ICMP_ECHO;
	icp->code = icp->checksum = icp->un.echo.sequence = 0;
	icp->un.echo.id = pid;	/* ID */

	/* compute ICMP checksum here */
	icp->checksum = in_cksum((unsigned short *) icp, DATALEN + 8);

	/* and send it out */
	j = sendto(sock_fp, (char *) outpack, DATALEN + 8, 0, &to,
		   sizeof(struct sockaddr));

	if (j < 0) {
	    int err = errno;

	    /* if our kernel tells us the network is unreachable we are done */
	    if (err == ENETUNREACH) {
#if defined(USE_SYSLOG)
		syslog(LOG_ERR, "network is unreachable (target: %s)", target);
#endif				/* USE_SYSLOG */

		return (ENETUNREACH);

	    } else {
#if defined(USE_SYSLOG)
		syslog(LOG_ERR, "sendto gave errno = %d\n", err);
#else				/* USE_SYSLOG */
		perror(progname);
#endif				/* USE_SYSLOG */

		if (softboot)
		    return (err);
	    }

	} else {
	    /* set the timeout value */
	    timeout.tv_sec = time;
	    timeout.tv_usec = 0;

	    /* wait for reply */
	    fdmask = 1 << sock_fp;
	    if (select(sock_fp + 1, (fd_set *) & fdmask, (fd_set *) NULL,
		       (fd_set *) NULL, &timeout) >= 1) {

		/* read reply */
		fromlen = sizeof(from);
		if (recvfrom(sock_fp, (char *) packet, DATALEN + MAXIPLEN + MAXICMPLEN, 0,
			     (struct sockaddr *) &from, &fromlen) < 0) {
		    int err = errno;

		    if (err != EINTR)
#if defined(USE_SYSLOG)
			syslog(LOG_ERR, "recvfrom gave errno = %d\n", err);
#else				/* USE_SYSLOG */
			perror(progname);
#endif				/* USE_SYSLOG */
		    if (softboot)
			return (err);

		    continue;
		}
		/* check if packet is our ECHO */
		icp = (struct icmphdr *) (packet + (((struct ip *) packet)->ip_hl << 2));

		if (icp->type == ICMP_ECHOREPLY) {
		    if (icp->un.echo.id == pid)
			/* got one back, that´ll do it for now */
			return (ENOERR);
		}
	    }
	}
    }
#if defined(USE_SYSLOG)
    syslog(LOG_ERR, "network is unreachable (target: %s)", target);
#endif				/* USE_SYSLOG */
    return (ENETUNREACH);
}

static int check_file_table(void)
{
    int fd;

    /* open a file */
    fd = open("/proc/uptime", O_RDONLY);
    if (fd == -1) {
	int err = errno;

	if (err == ENFILE) {
	    /* we need a reboot if ENFILE is returned (file table overflow) */
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "file table overflow detected!\n");
#endif				/* USE_SYSLOG */
	    return (ENFILE);
	} else {
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "cannot open /proc/uptime (errno = %d)", err);
#else				/* USE_SYSLOG */
	    perror(progname);
#endif				/* USE_SYSLOG */

	    if (softboot)
		return (err);
	}
    } else {
	if (close(fd) < 0) {
	    int err = errno;

#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "close /proc/uptime gave errno = %d", err);
#else				/* USE_SYSLOG */
	    perror(progname);
#endif				/* USE_SYSLOG */
	    if (softboot)
		return (err);
	}
    }

    return (ENOERR);
}

/* execute test binary */
static int testbin(char *tbinary)
{
    pid_t child_pid;
    int result, res;

    if (tbinary == NULL)
	return (ENOERR);

    child_pid = fork();
    if (!child_pid) {
	execl(tbinary, tbinary, NULL);

	/* execl should only return in case of an error */
	/* so we return that error */
	exit(errno);
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
    res = WEXITSTATUS(result);
    if (res != 0) {
#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "test binary returned %d", res);
#endif				/* USE_SYSLOG */
	return (res);
    }
    return (ENOERR);
}

static int check_temp(void)
{
    unsigned char temperature;

    /* is the temperature device open? */
    if (temp == -1)
	return (ENOERR);

    /* read the line (there is only one) */
    if (read(temp, &temperature, sizeof(temperature)) < 0) {
	int err = errno;

#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "read %s gave errno = %d", err, tempname);
#else				/* USE_SYSLOG */
	perror(progname);
#endif				/* USE_SYSLOG */
	if (softboot)
	    return (err);

	return (ENOERR);
    }
#if defined(USE_SYSLOG)
    if (verbose)
	syslog(LOG_INFO, "current temperature is %d", temperature);

    if (temperature > templevel3) {
	if (!have3) {
	    /* once we reach level3, issue a warning */
	    syslog(LOG_WARNING, "temperature increases %d", templevel3);
	    have1 = have2 = have3 = TRUE;
	}
    } else {
	have3 = FALSE;
	if (temperature > templevel2) {
	    if (!have2) {
		/* once we reach level2, issue a warning */
		syslog(LOG_WARNING, "temperature increases %d", templevel2);
		have1 = have2 = TRUE;
	    }
	} else {
	    have2 = have3 = FALSE;
	    if (temperature > templevel1) {
		if (!have1) {
		    /* once we reach level1, issue a warning */
		    syslog(LOG_WARNING, "temperature increases %d", templevel1);
		    have1 = TRUE;
		}
	    }
	}
    }
#endif				/* USE_SYSLOG */

    if (temperature >= maxtemp) {
#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "it is too hot inside (temperature = %d)", temperature);
#else				/* USE_SYSLOG */
	fprintf(stderr, "%s: it is too hot inside (temperature = %d\n", progname, temperature);
#endif				/* USE_SYSLOG */

	return (ETOOHOT);
    }
    return (ENOERR);
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
	return (ret);
    }
    return (ENOERR);
}

static void wd_action(int result, char *rbinary)
{
    /* no error, keep on working */
    if (result == ENOERR)
	return;

    /* error that might be repairable */
    if (result != EREBOOT)
	result = repair(rbinary, result);

    /* if still error, reboot */
    if (result != ENOERR)
	do_shutdown(result);
}
int main(int argc, char *const argv[])
{
    FILE *fp;
    int c, tint = SLEEP_INTERVAL, force = FALSE, sync_it = FALSE;
    int hold = 48 * 1024, sock_fp = -1;
    char *filename = NULL, *target = NULL, *tbinary = NULL;
    char *rbinary = NULL;
    struct sockaddr to;
    unsigned char *packet;
    struct protoent *proto;
    pid_t child_pid;

#if defined(USE_SYSLOG)
    char log[256], *opts = "d:i:n:fsvbl:p:t:c:r:m:a:";
    long count = 0L;
#else				/* USE_SYSLOG */
    char *opts = "d:i:n:fsbl:p:t:c:r:m:a:";
#endif				/* USE_SYSLOG */

    progname = basename(argv[0]);
    /* check the options */
    while ((c = getopt(argc, argv, opts)) != EOF) {
	switch (c) {
	case 'n':
	    filename = optarg;
	    break;
	case 'p':
	    target = optarg;
	    break;
	case 'a':
	    admin = optarg;
	    break;
	case 'd':
	    devname = optarg;
	    break;
	case 't':
	    tempname = optarg;
	    break;
	case 'm':
	    maxtemp = atoi(optarg);;
	    break;
	case 'i':
	    tint = atoi(optarg);
	    break;
	case 'c':
	    tbinary = optarg;
	    break;
	case 'r':
	    rbinary = optarg;
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
	case 'l':
	    maxload1 = atoi(optarg);
	    maxload5 = maxload1 * 3 / 4;
	    maxload15 = maxload1 / 2;
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
	/* setup the socket */
	memset(&to, 0, sizeof(struct sockaddr));

	((struct sockaddr_in *) &to)->sin_family = AF_INET;
	if ((((struct sockaddr_in *) &to)->sin_addr.s_addr = inet_addr(target)) == (unsigned int) -1) {
	    (void) fprintf(stderr, "%s: unknown host %s\n", progname, target);
	    exit(1);
	}
	if (!(packet = (unsigned char *) malloc((unsigned int) (DATALEN + MAXIPLEN + MAXICMPLEN)))) {
	    fprintf(stderr, "%s: out of memory\n", progname);
	    exit(1);
	}
	if (!(proto = getprotobyname("icmp"))) {
	    (void) fprintf(stderr, "%s: unknown protocol icmp.\n", progname);
	    exit(1);
	}
	if ((sock_fp = socket(AF_INET, SOCK_RAW, proto->p_proto)) < 0) {
	    perror(progname);
	    exit(1);
	}
	(void) setsockopt(sock_fp, SOL_SOCKET, SO_RCVBUF, (char *) &hold,
			  sizeof(hold));
    }
    
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
#if defined(USE_SYSLOG)
    /* Okay, we're a daemon     */
    /* but we're still attached to the tty */
    /* create our own session */
    setsid();

    /* with USE_SYSLOG we don't do any console IO */
    close(0);
    close(1);
    close(2);

    /* Log the starting message */
    openlog(progname, LOG_PID, LOG_DAEMON);
    sprintf(log, "starting daemon (%d.%d):", MAJOR_VERSION, MINOR_VERSION);
    sprintf(log + strlen(log), " int=%ds sync=%s soft=%s mla=%d ping=%s file=%s test=%s repair=%s alive=%s temp=%s to=%s",
	    tint,
	    sync_it ? "yes" : "no",
	    softboot ? "yes" : "no",
	    maxload1,
	    (target == NULL) ? "none" : target,
	    (filename == NULL) ? "none" : filename,
	    (tbinary == NULL) ? "none" : tbinary,
	    (rbinary == NULL) ? "none" : rbinary,
	    (devname == NULL) ? "none" : devname,
	    (tempname == NULL) ? "none" : tempname,
#if defined(SENDTOADMIN)
	    admin);
#else
	    "noone");
#endif				/* SENDTOADMIN */
    syslog(LOG_INFO, log);
#endif				/* USE_SYSLOG */

    /* open the device */
    if (devname != NULL) {
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
    
    if (tempname != NULL) {
	/* open the temperature file */
	temp = open("/dev/temperature", O_RDONLY);
	if (temp == -1) {
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "cannot open /dev/temperature (errno = %d)", errno);
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

    /* main loop: update after <tint> seconds */
    while (1) {
	wd_action(keep_alive(), rbinary);

	/* sync system if we have to */
	if (sync_it)
	    sync_system();

	wd_action(keep_alive(), rbinary);

	/* check process table */
	wd_action(check_fork(), rbinary);

	wd_action(keep_alive(), rbinary);

	/* check file table */
	wd_action(check_file_table(), rbinary);

	wd_action(keep_alive(), rbinary);

	/* check load average */
	wd_action(check_load(), rbinary);

	wd_action(keep_alive(), rbinary);

	/* check temperature */
	wd_action(check_temp(), rbinary);

	wd_action(keep_alive(), rbinary);

	/* in filemode stat file */
	wd_action(check_file_stat(filename), rbinary);

	wd_action(keep_alive(), rbinary);

	/* in network mode ping the ip address */
	wd_action(check_net(target, sock_fp, to, packet, tint / 3), rbinary);

	wd_action(keep_alive(), rbinary);

	/* in user mode execute the given binary */
	wd_action(testbin(tbinary), rbinary);

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
