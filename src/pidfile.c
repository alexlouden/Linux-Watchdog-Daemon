#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include "extern.h"
#include "watch_err.h"

#if USE_SYSLOG
#include <syslog.h>
#endif

int check_pidfile(struct list *file)
{
    int fd = open(file->name, O_RDONLY), pid;
    char buf[10];
    
    if (fd == -1) {
	int err = errno;

#if USE_SYSLOG
	syslog(LOG_ERR, "cannot open %s (errno = %d = '%m')", file->name, err);
#else				/* USE_SYSLOG */
	perror(progname);
#endif				/* USE_SYSLOG */

	/* on error ENETDOWN|ENETUNREACH we react as if we're in ping mode */
	if (softboot || err == ENETDOWN || err == ENETUNREACH)
	    return (err);
	
	return(ENOERR);
    }
    
    /* position pointer at start of file */
    if (lseek(fd, 0, SEEK_SET) < 0) {
	int err = errno;
        
#if USE_SYSLOG
	syslog(LOG_ERR, "lseek %s gave errno = %d = '%m'", file->name, err);
#else                           /* USE_SYSLOG */
        perror(progname);
#endif                          /* USE_SYSLOG */

	close(fd);
        if (softboot)
	        return (err);

        return (ENOERR);
    }

    /* read the line (there is only one) */
    if (read(fd, buf, sizeof(buf)) < 0) {
    	int err = errno;

#if USE_SYSLOG
        syslog(LOG_ERR, "read %s gave errno = %d = '%m'", file->name, err);
#else                           /* USE_SYSLOG */
        perror(progname);
#endif                          /* USE_SYSLOG */

	close(fd);
        if (softboot)
	        return (err);

        return (ENOERR);
    }

    /* we only care about integer values */
    pid = atoi(buf);
    
    if (close(fd) == -1) {
     	int err = errno;

#if USE_SYSLOG
        syslog(LOG_ERR, "could not close %s, errno = %d = '%m'", file->name, err);
#else                           /* USE_SYSLOG */
        perror(progname);
#endif                          /* USE_SYSLOG */

        if (softboot)
	        return (err);

        return (ENOERR);
    }

    if (kill (pid, 0) == -1) {
	int err = errno;
	
#if USE_SYSLOG
        syslog(LOG_ERR, "pinging process %d (%s) gave errno = %d = '%m'", pid, file->name, err);
#else                           /* USE_SYSLOG */
        perror(progname);
#endif                          /* USE_SYSLOG */

        if (softboot || err == ESRCH)
	        return (err);

        return (ENOERR);
    }
    
#if USE_SYSLOG
    /* do verbose logging */
    if (verbose)
	syslog(LOG_INFO, "was able to ping process %d (%s).", pid, file->name);
#endif

    return (ENOERR);
}
