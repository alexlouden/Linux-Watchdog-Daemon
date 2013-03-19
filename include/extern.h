#ifndef _EXTERN_H_
#define _EXTERN_H_

#include <netinet/in.h>
#include <stdio.h>

#include "logmessage.h"
#include "xmalloc.h"

/* === Variable types === */
struct pingmode {
	struct sockaddr to;
	int sock_fp;
	unsigned char *packet;
};

struct filemode {
	int mtime;
};

struct ifmode {
	unsigned long bytes;
};

union wdog_options {
	struct pingmode net;
	struct filemode file;
	struct ifmode iface;
};

struct list {
	char *name;
	union wdog_options parameter;
	struct list *next;
};

/* === Constants === */

#define DATALEN         (64 - 8)
#define MAXIPLEN        60
#define MAXICMPLEN      76
#define MAXPACKET       (65536 - 60 - 8)	/* max packet size */

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define TS_SIZE	12

/* === External variables === */
#if 1
extern int tint;
extern int logtick;
extern int ticker;
extern int schedprio;
extern int maxload1;
extern int maxload5;
extern int maxload15;
extern int minpages;
extern int maxtemp;
extern int pingcount;

extern char *devname;
extern char *admin;
extern char *tempname;

extern time_t	test_timeout;		/* test-binary time out value. */
extern time_t	repair_timeout;		/* repair-binary time out value. */
extern int		dev_timeout;		/* Watchdog hardware time-out. */

extern char *logdir;

extern char *heartbeat;
extern int hbstamps;

extern int realtime;

extern struct list *tr_bin_list;
extern char *test_dir;

extern struct list *file_list;
extern struct list *target_list;
extern struct list *pidfile_list;
extern struct list *iface_list;


extern char *tbinary;
extern char *rbinary;

extern char *sendmail_bin;

/* = Not (yet) from config file. = */

extern int softboot;
extern int verbose;
extern char *filename_buf;
extern pid_t daemon_pid;

#endif

/* === Function prototypes === */

#ifndef GCC_NORETURN
#ifdef __GNUC__
#define GCC_NORETURN __attribute__((noreturn))
#else
#define GCC_NORETURN
#endif				/*!__GNUC__ */
#endif				/*!GCC_NORETURN */

/** file_stat.c **/
int check_file_stat(struct list *);

/** file_table.c **/
int check_file_table(void);

/** keep_alive.c **/
int open_watchdog(char *name, int timeout);
int set_watchdog_timeout(int timeout);
int keep_alive(void);
int get_watchdog_fd(void);
int close_watchdog(void);

/** load.c **/
int open_loadcheck(void);
int check_load(void);
int close_loadcheck(void);

/** net.c **/
int check_net(char *target, int sock_fp, struct sockaddr to, unsigned char *packet, int time, int count);
int open_netcheck(struct list *tlist);

/** temp.c **/
int open_tempcheck(char *name);
int check_temp(void);
int close_tempcheck(void);

/** test_binary.c **/
int check_bin(char *, time_t, int);

/** pidfile.c **/
int check_pidfile(struct list *);

/** iface.c **/
int check_iface(struct list *);

/** memory.c **/
int open_memcheck(void);
int check_memory(void);
int close_memcheck(void);

/** shutdown.c **/
void do_shutdown(int errorcode);
void sigterm_handler(int arg);
void terminate(void) GCC_NORETURN;

/** heartbeat.c **/
int open_heartbeat(void);
int write_heartbeat(void);
int close_heartbeat(void);

/** lock_mem.c **/
void lock_our_memory(int do_lock, int priority, pid_t pid);
void unlock_our_memory(void);

/** daemon-pid.c **/
int write_pid_file(const char *fname);

#endif /*_EXTERN_H_*/
