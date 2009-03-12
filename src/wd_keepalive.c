/**********************************************************
 * Copyright    Appliance Studio Ltd
 *
 * Filename:    $Id: keepalive.c,v 1.1.1.1 2001/02/27 13:36:56 marcel Exp $    
 * Author:      Marcel Jansen, 22 February 2001
 * Purpose:     This program can be run during critical periods
 *              when the normal watcdog shouldn't be run. It will
 *              read from the same configuration file, it will do
 *              no checks but will keep writing to the device
 * History:     
 * $Log: keepalive.c,v $
 * Revision 1.1.1.1  2001/02/27 13:36:56  marcel
 * initial version
 *
 *
***********************************************************/

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#define __USE_GNU
#include <string.h>
#include <syslog.h>

#define TRUE  1
#define FALSE 0


#define MAJOR_VERSION   1
#define MINOR_VERSION     0

#define DEVICE		    "watchdog-device"
#define INTERVAL	    "interval"
#define CONFIG_FILENAME "/etc/watchdog.conf"
#define PIDFILE "/var/run/watchdog.pid"
/* config file line length */
#define CONFIG_LINE_LEN 160


int watchdog = -1, tint = 10, schedprio = 1;
char *devname = NULL, *progname = NULL;

#if defined(_POSIX_MEMLOCK)
int mlocked = FALSE, realtime = FALSE;
#endif



static void usage(void)
{
    fprintf(stderr, "%s version %d.%d, usage:\n", progname, MAJOR_VERSION, MINOR_VERSION);
    fprintf(stderr, "%s \n", progname);
    exit(1);
}



/* write a log entry on exit */
static void log_end()
{
    /* Log the closinging message */
    syslog(LOG_INFO, "stopping keepalive daemon (%d.%d)", MAJOR_VERSION, MINOR_VERSION);
    closelog();
    sleep(5);           /* make sure log is written */
    return;
}

/* close the device and check for error */
static void close_all()
{
    if ( watchdog != -1 && close(watchdog) == -1 ) {
        syslog(LOG_ALERT, "cannot close %s", devname);
    }
}


/* on exit we close the device and log that we stop */
void terminate(int arg) {
#if defined(_POSIX_MEMLOCK)
    if ( realtime == TRUE && mlocked == TRUE ) {
        /* unlock all locked pages */
        if ( munlockall() != 0 ) {
            syslog(LOG_ERR, "cannot unlock realtime memory (errno = %d)", errno);
        }
    }
#endif    
    close_all();
    log_end();
    exit(0);
}

static int spool(char *line, int *i, int offset)
{
    for ( (*i) += offset; line[*i] == ' ' || line[*i] == '\t'; (*i)++ );
    if ( line[*i] == '=' )
        (*i)++;
    for ( ; line[*i] == ' ' || line[*i] == '\t'; (*i)++ );
    if ( line[*i] == '\0' )
        return(1);
    else
        return(0);
}

static void read_config(char *filename, char *progname)
{
    FILE *wc;

    if ( (wc = fopen(filename, "r")) == NULL ) {
        perror(progname);
        exit(1);
    }

    while ( !feof(wc) ) {
        char line[CONFIG_LINE_LEN];

        if ( fgets(line, CONFIG_LINE_LEN, wc) == NULL ) {
            if ( !ferror(wc) )
                break;
            else {
                perror(progname);
                exit(1);
            }
        }
        else {
            int i, j;

            /* scan the actual line for an option */
            /* first remove the leading blanks */
            for ( i = 0; line[i] == ' ' || line[i] == '\t'; i++ );

            /* if the next sign is a '#' we have a comment */
            if ( line[i] == '#' )
                continue;

            /* also remove the trailing blanks and the \n */
            for ( j = strlen(line) - 1; line[j] == ' ' || line[j] == '\t' || line[j] == '\n'; j-- );
            line[j + 1] = '\0';

            /* if the line is empty now, we don't have to parse it */
            if ( strlen(line + i) == 0 )
                continue;

            /* now check for an option */
            if ( strncmp(line + i, INTERVAL, strlen(INTERVAL)) == 0 ) {
                if ( spool(line, &i, strlen(INTERVAL)) )
                    fprintf(stderr, "Ignoring invalid line in config file:\n%s\n", line);
                else
                    tint = atol(line + i);
            }
            else if ( strncmp(line + i, DEVICE, strlen(DEVICE)) == 0 ) {
                if ( spool(line, &i, strlen(DEVICE)) )
                    devname = NULL;
                else
                    devname = strdup(line + i);
            }
            else {
                fprintf(stderr, "Ignoring config line: %s\n", line);
            }
        }
    }

    if ( fclose(wc) != 0 ) {
        perror(progname);
        exit(1);
    }
}


int main(int argc, char *const argv[])
{
    FILE *fp;
    char log[256];
    char *filename = CONFIG_FILENAME;
    pid_t child_pid;
    int count = 0;

    progname = basename(argv[0]);

    read_config(filename, progname);

    /* make sure we're on the root partition */
    if ( chdir("/") < 0 ) {
        perror(progname);
        exit(1);
    }
#if !defined(DEBUG)
    /* fork to go into the background */
    if ( (child_pid = fork()) < 0 ) {
        perror(progname);
        exit(1);
    }
    else if ( child_pid > 0 ) {
        /* fork was okay          */
        /* wait for child to exit */
        if ( waitpid(child_pid, NULL, 0) != child_pid ) {
            perror(progname);
            exit(1);
        }
        /* and exit myself */
        exit(0);
    }
    /* and fork again to make sure we inherit all rights from init */
    if ( (child_pid = fork()) < 0 ) {
        perror(progname);
        exit(1);
    }
    else if ( child_pid > 0 )
        exit(0);
#endif				/* !DEBUG */

    /* now we're free */
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
    sprintf(log, "starting keepalive daemon (%d.%d):", MAJOR_VERSION, MINOR_VERSION);
    sprintf(log + strlen(log), " int=%d alive=%s", tint, devname);
    syslog(LOG_INFO, log);

    /* open the device */
    if ( devname != NULL ) {
        watchdog = open(devname, O_WRONLY);
        if ( watchdog == -1 ) {
            syslog(LOG_ERR, "cannot open %s (errno = %d = '%m')", devname, errno);
            exit(1);
        }
    }

    /* tuck my process id away */
    fp = fopen(PIDFILE, "w");
    if ( fp != NULL ) {
        fprintf(fp, "%d\n", getpid());
        (void) fclose(fp);
    }

    /* set signal term to call terminate() */
    /* to make sure watchdog device is closed */
    signal(SIGTERM, terminate);

#if defined(_POSIX_MEMLOCK)
    if ( realtime == TRUE ) {
        /* lock all actual and future pages into memory */
        if ( mlockall(MCL_CURRENT | MCL_FUTURE) != 0 ) {
            syslog(LOG_ERR, "cannot lock realtime memory (errno = %d = '%m')", errno);
        }
        else {
            struct sched_param sp;

            /* now set the scheduler */
            sp.sched_priority = schedprio;
            if ( sched_setscheduler(0, SCHED_RR, &sp) != 0 ) {
                syslog(LOG_ERR, "cannot set scheduler (errno = %d = '%m')", errno);
            }
            else
                mlocked = TRUE;
        }
    }
#endif

    /* main loop: update after <tint> seconds */
    while ( 1 ) {
        if ( write(watchdog, "\0", 1) < 0 ) {
            int err = errno;
            syslog(LOG_ERR, "write watchdog device gave error %d = '%m'!", err);
        }

        /* finally sleep some seconds */
        sleep(tint);

        count++;
        syslog(LOG_INFO, "still alive after %ld seconds = %ld interval(s)", count * tint, count);
    }
}


