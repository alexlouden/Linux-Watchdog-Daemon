#ifndef _WATCH_ERR_H
#define _WATCH_ERR_H

/*********************************/
/* additional error return codes */
/*********************************/

#define ENOERR		0	/* no error */
#define EREBOOT		201	/* unconditional reboot */
#define ERESET		202	/* unconditional hard reset */
#define EMAXLOAD	203	/* load average too high */
#define ETOOHOT		204	/* too hot inside */
#define ENOLOAD		205	/* /proc/loadavg contains no data */
#define ENOCHANGE	206	/* file wasn't changed in the given interval */
#define EINVMEM		207	/* /proc/meminfo contains invalid data */
#define ECHKILL		208	/* child was killed by signal */
#define ETOOLONG	209	/* child didn't return in time */

#endif /*_WATCH_ERR_H*/
