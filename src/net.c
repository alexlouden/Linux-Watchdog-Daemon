#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <sys/time.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include "extern.h"
#include "watch_err.h"

#if USE_SYSLOG
#include <syslog.h>
#endif

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

int check_net(char *target, int sock_fp, struct sockaddr to, unsigned char *packet, int time)
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
#if USE_SYSLOG
		syslog(LOG_ERR, "network is unreachable (target: %s)", target);
#endif				/* USE_SYSLOG */

		return (ENETUNREACH);

	    } else {
#if USE_SYSLOG
		syslog(LOG_ERR, "sendto gave errno = %d = '%m'\n", err);
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
#if USE_SYSLOG
			syslog(LOG_ERR, "recvfrom gave errno = %d = '%m'\n", err);
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
#if USE_SYSLOG
    syslog(LOG_ERR, "network is unreachable (target: %s)", target);
#endif				/* USE_SYSLOG */
    return (ENETUNREACH);
}
