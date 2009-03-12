#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include "extern.h"
#include "watch_err.h"

#if defined(USE_SYSLOG)
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
#if defined(USE_SYSLOG)
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
	    /* check if actual child terminated */
	    if (ret == child_pid)
		child_pid = 0;
	} while (ret > 0 && WEXITSTATUS(result) == 0);

	/* check result */
	/* if one of the scripts returns an error code just return that code */
	res = WEXITSTATUS(result);
	if (res != 0) {
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "test binary returned %d", res);
#endif				/* USE_SYSLOG */
	    return (res);
	}
	/* give an error in case there are still old childs running */
	/* in fact if an old one hangs around we already got an error */
	/* message in earlier rounds */
	if (ret == 0 && child_pid != 0) {
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "child %d did not exit immediately (error = %d)", child_pid, err);
#else				/* USE_SYSLOG */
	    perror(progname);
#endif				/* USE_SYSLOG */
	    if (softboot)
		return (err);
	}
    }
    return (ENOERR);
}
