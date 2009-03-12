#include <netinet/in.h>

/* external variables */
extern int softboot, watchdog, temp, maxtemp, tint;
extern int maxload1, maxload5, maxload15, load, verbose;
extern pid_t pid;
extern char *tempname, *admin, *devname, *progname;

/* variable types */
struct pingmode
{
	struct sockaddr to;
	int sock_fp;
	unsigned char *packet;
};

struct filemode
{
	int mtime;
};

union options
{
        struct pingmode net;
        struct filemode file;
};
                                        
struct list
{
        char *name;
        union options parameter;
        struct list *next;
};

/* constants */
#define DATALEN         (64 - 8)
#define MAXIPLEN        60
#define MAXICMPLEN      76
#define MAXPACKET       (65536 - 60 - 8)        /* max packet size */

#if !defined(MAXTEMP)
#define MAXTEMP 120
#endif                          /* !MAXTEMP */

#if !defined(TIMER_MARGIN)
#define TIMER_MARGIN 60
#endif                          /* !TIMER_MARGIN */

#define TRUE 1
#define FALSE 0

/* function prototypes */
int check_file_stat(struct list *);
int check_file_table(void);
int keep_alive(void);
int check_load(void);
int check_net(char *target, int sock_fp, struct sockaddr to, unsigned char *packet, int time);
int check_temp(void);
int check_bin(char *);

void do_shutdown(int errorcode);
void terminate(int arg);
