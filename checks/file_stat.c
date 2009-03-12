#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include "extern.h"
#include "watch_err.h"

#if defined(USE_SYSLOG)
#include <syslog.h>
#endif

int check_file_stat(struct list *file)
{
    struct stat buf;

    /* in filemode stat file */
    if (stat(file->name, &buf) == -1) {
	int err = errno;

#if defined(USE_SYSLOG)
	syslog(LOG_ERR, "cannot stat %s (errno = %d)", file->name, err);
#else				/* USE_SYSLOG */
	perror(progname);
#endif				/* USE_SYSLOG */
	/* on error ENETDOWN|ENETUNREACH we react as if we're in network mode */
	if (softboot || err == ENETDOWN || err == ENETUNREACH)
	    return (err);
    } else if (file->parameter.file.mtime != 0) {

#if defined(USE_SYSLOG)
	/* do verbose logging */
	if (verbose)
	    syslog(LOG_INFO, "file %s was last changed at %s.", file->name, ctime(&buf.st_mtime));
#endif

	if (time(NULL) - buf.st_mtime > file->parameter.file.mtime) {
	    /* file wasn't changed often enough */
#if defined(USE_SYSLOG)
	    syslog(LOG_ERR, "file %s was not changed in %d seconds.", file->name, file->parameter.file.mtime);
#else				/* USE_SYSLOG */
	    fprintf(stderr, "file %s was not changed in %d seconds.", file->name, file->parameter.file.mtime);
#endif				/* USE_SYSLOG */
	    return (ENOCHANGE);
	}
    }
    return (ENOERR);
}
