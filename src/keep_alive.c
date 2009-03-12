#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <sys/syslog.h>
#include <unistd.h>

#include "extern.h"
#include "watch_err.h"

/* write to the watchdog device */
int keep_alive(void)
{
    if (watchdog == -1)
	return (ENOERR);

    if (write(watchdog, "\0", 1) < 0) {
	int err = errno;

#if USE_SYSLOG
	syslog(LOG_ERR, "write watchdog device gave error %d = '%m'!", err);
#endif
	if (softboot)
	    return (err);
    }
    return (ENOERR);
}
