#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include "extern.h"
#include "watch_err.h"

#if USE_SYSLOG
#include <syslog.h>
#endif

/* execute test binary */
int check_bin(char *tbinary)
{
    pid_t child_pid;
    int result, res;

    child_pid = fork();
    if (!child_pid) {
	/* child, exit immediately, if no test binary given */
	if (tbinary == NULL)
	    exit(0);

	/* else start binary */
	execl(tbinary, tbinary, NULL);

	/* execl should only return in case of an error */
	/* so we return that error */
	exit(errno);
    } else if (child_pid < 0) {	/* fork failed */
	int err = errno;

 	if (errno == EAGAIN) {	/* process table full */
#if USE_SYSLOG
	    syslog(LOG_ERR, "process table is full!");
#endif				/* USE_SYSLOG */
	    return (EREBOOT);
	} else if (softboot)
	    return (err);
	else
	    return (ENOERR);
    } else {
	int ret, err;

	/* fork was okay          */
	/* wait for child(s) to stop */
	/* but only after a short sleep */
	sleep(tint >> 1);

	do {
	    ret = waitpid(-1, &result, WNOHANG);
	    err = errno;
	} while (ret > 0 && WIFEXITED(result) != 0 && WEXITSTATUS(result) == 0);

	/* check result: */
	/* ret < 0 			=> error */
	/* ret == 0			=> no more child returned, however we may already have caught the actual child */
	/* WIFEXITED(result) == 0	=> child did not exit normally but was killed by signal which was not caught */
	/* WEXITSTATUS(result) != 0	=> child returned an error code */
	if (ret > 0) {
		if (WIFEXITED(result) != 0) {
			/* if one of the scripts returns an error code just return that code */
#if USE_SYSLOG
			syslog(LOG_ERR, "test binary returned %d", WEXITSTATUS(result));
#endif				/* USE_SYSLOG */
		    	return (WEXITSTATUS(result));
		} else if (WIFSIGNALED(result) != 0)  {
			/* if one of the scripts was killed return ECHKILL */
#if USE_SYSLOG
			syslog(LOG_ERR, "test binary was killed by uncaught signal %d", WTERMSIG(result));
#endif				/* USE_SYSLOG */
		    	return (ECHKILL);
		}
	} else {
		/* in case there are still old childs running due to an error */
		/* log that error */
		if (err != 0 && err != ECHILD) {
#if USE_SYSLOG
		    errno = err;
		    syslog(LOG_ERR, "child %d did not exit immediately (error = %d = '%m')", child_pid, err);
#else				/* USE_SYSLOG */
		    perror(progname);
#endif				/* USE_SYSLOG */
		    if (softboot)
			return (err);
		}
	}
    }
    return (ENOERR);
}
