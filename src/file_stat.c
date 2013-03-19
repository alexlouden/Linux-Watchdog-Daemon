#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#include "extern.h"
#include "watch_err.h"

int check_file_stat(struct list *file)
{
	struct stat buf;

	/* in filemode stat file */
	if (stat(file->name, &buf) == -1) {
		int err = errno;
		log_message(LOG_ERR, "cannot stat %s (errno = %d = '%s')", file->name, err, strerror(err));
		/* on error ENETDOWN|ENETUNREACH we react as if we're in ping mode */
		if (softboot || err == ENETDOWN || err == ENETUNREACH)
			return (err);
	} else if (file->parameter.file.mtime != 0) {

		/* do verbose logging */
		if (verbose && logtick && ticker == 1) {
			char text[25];
			/* Remove the trailing '\n' of the ctime() formatted string. */
			strncpy(text, ctime(&buf.st_mtime), sizeof(text)-1);
			text[sizeof(text)-1] = 0;
			log_message(LOG_INFO, "file %s was last changed at %s", file->name, text);
		}

		if (time(NULL) - buf.st_mtime > file->parameter.file.mtime) {
			/* file wasn't changed often enough */
			log_message(LOG_ERR, "file %s was not changed in %d seconds.", file->name, file->parameter.file.mtime);
			return (ENOCHANGE);
		}
	}
	return (ENOERR);
}
