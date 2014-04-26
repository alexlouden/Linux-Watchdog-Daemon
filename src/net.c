/* > net.c
 *
 * Code for checking network access. The open_netcheck() funcion is from set-up
 * code originally in watchdog.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <sys/time.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>		/* for gethostname() etc */
#include <netdb.h>		/* for gethostbyname() */
#include <sys/param.h>	/* for MAXHOSTNAMELEN */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "extern.h"
#include "watch_err.h"

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
	}			/* mop up an odd byte, if necessary */
	if (nleft == 1) {
		sum += htons(*(u_char *) w << 8);
	}
	/* add back carry outs from top 16 bits to low 16 bits */
	sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
	sum += (sum >> 16);	/* add carry */
	answer = ~sum;		/* truncate to 16 bits */
	return (answer);
}

int check_net(char *target, int sock_fp, struct sockaddr to, unsigned char *packet, int time, int count)
{
	int i;
	unsigned char outpack[MAXPACKET];

	if (target == NULL)
		return (ENOERR);

	/* try "ping-count" times */
	for (i = 0; i < count; i++) {

		struct sockaddr_in from;
		int fdmask, j;
		socklen_t fromlen;
		struct timeval timeout, dtimeout;
		struct icmphdr *icp = (struct icmphdr *)outpack;

		/* setup a ping message */
		icp->type = ICMP_ECHO;
		icp->code = icp->checksum = 0;
		icp->un.echo.sequence = htons(i + 1);
		icp->un.echo.id = daemon_pid;	/* ID */

		/* compute ICMP checksum here */
		icp->checksum = in_cksum((unsigned short *)icp, DATALEN + 8);

		/* and send it out */
		j = sendto(sock_fp, (char *)outpack, DATALEN + 8, 0, &to, sizeof(struct sockaddr));

		if (j < 0) {
			int err = errno;

			/* if our kernel tells us the network is unreachable we are done */
			if (err == ENETUNREACH) {
				log_message(LOG_ERR, "network is unreachable (target: %s)", target);

				return (ENETUNREACH);

			} else {
				log_message(LOG_ERR, "sendto gave errno = %d = '%s'", err, strerror(err));

				if (softboot)
					return (err);
			}

		} else {
			gettimeofday(&timeout, NULL);
			/* set the timeout value */
			j = time / count;
			/* we have to wait at least one second */
			if (j == 0)
				j = 1;
			timeout.tv_sec += j;

			/* wait for reply */
			fdmask = 1 << sock_fp;
			while (1) {
				gettimeofday(&dtimeout, NULL);
				dtimeout.tv_sec = timeout.tv_sec - dtimeout.tv_sec;
				dtimeout.tv_usec = timeout.tv_usec - dtimeout.tv_usec;
				if (dtimeout.tv_usec < 0) {
					dtimeout.tv_usec += 1000000;
					dtimeout.tv_sec--;
				}
				/* Is this loop really needed? I have yet to see a usec value >= 1000000. */
				while (dtimeout.tv_usec >= 1000000) {
					dtimeout.tv_usec -= 1000000;
					dtimeout.tv_sec++;
				}
				if (dtimeout.tv_sec < 0)
					break;

				if (verbose && logtick && ticker == 1)
					log_message(LOG_ERR, "ping select timeout = %2ld.%06ld seconds",
					       dtimeout.tv_sec, dtimeout.tv_usec);

				if (select
				    (sock_fp + 1, (fd_set *) & fdmask, (fd_set *) NULL, (fd_set *) NULL,
				     &dtimeout) >= 1) {

					/* read reply */
					fromlen = sizeof(from);
					if (recvfrom
					    (sock_fp, (char *)packet, DATALEN + MAXIPLEN + MAXICMPLEN, 0,
					     (struct sockaddr *)&from, &fromlen) < 0) {
						int err = errno;

						if (err != EINTR)
							log_message(LOG_ERR, "recvfrom gave errno = %d = '%s'", err, strerror(err));

						if (softboot)
							return (err);

						continue;
					}

					/* check if packet is our ECHO */
					icp = (struct icmphdr *)(packet + (((struct ip *)packet)->ip_hl << 2));

					if (icp->type == ICMP_ECHOREPLY && icp->un.echo.id == daemon_pid) {
						if (from.sin_addr.s_addr ==
						    ((struct sockaddr_in *)&to)->sin_addr.s_addr) {

							if (verbose && logtick && ticker == 1)
								log_message(LOG_INFO, "got answer from target %s", target);

							return (ENOERR);
						}
					}
				}
			}
		}
	}

	log_message(LOG_ERR, "no response from ping (target: %s)", target);

	return (ENETUNREACH);
}

/*
 * Set up pinging if in ping mode
 */

int open_netcheck(struct list *tlist)
{
	struct list *act;
	int hold = 0;

	if (tlist != NULL) {
		for (act = tlist; act != NULL; act = act->next) {
			struct protoent *proto;
			struct pingmode *net = &act->parameter.net; /* 'net' is alias of act->parameter.net */

			/* setup the socket */
			memset(&(net->to), 0, sizeof(struct sockaddr));

			((struct sockaddr_in *)&(net->to))->sin_family = AF_INET;
			if ((((struct sockaddr_in *)&(net->to))->sin_addr.s_addr =
			     inet_addr(act->name)) == (unsigned int)-1) {
			     fatal_error(EX_USAGE, "unknown host %s", act->name);
			}
			net->packet = (unsigned char *)xcalloc((unsigned int)(DATALEN + MAXIPLEN + MAXICMPLEN), sizeof(char));
			if (!(proto = getprotobyname("icmp"))) {
				fatal_error(EX_SYSERR, "unknown protocol icmp.");
			}
			if ((net->sock_fp = socket(AF_INET, SOCK_RAW, proto->p_proto)) < 0
			    || fcntl(net->sock_fp, F_SETFD, 1)) {
			    fatal_error(EX_SYSERR, "error opening socket (%s)", strerror(errno));
			}

			/* this is necessary for broadcast pings to work */
			(void)setsockopt(net->sock_fp, SOL_SOCKET, SO_BROADCAST, (char *)&hold, sizeof(hold));

			hold = 48 * 1024;
			(void)setsockopt(net->sock_fp, SOL_SOCKET, SO_RCVBUF, (char *)&hold, sizeof(hold));
		}
	}

	return 0;
}
