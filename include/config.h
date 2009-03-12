/* If you don't want to log any activity uncomment the following.
 * I strongly discourage this, though.
 */
 
#define USE_SYSLOG

/*
 * How long does watchdog sleep between two passes? 10s is the default.
 */

/* #define SLEEP_INTERVAL 10 */

/*
 * If you do not want to send any mail comment the following line.
 */

#define SENDTOADMIN

/*
 * Address to mail to. Default is root
 */
 
/* #define SYSADMIN "root"*/

/*
 * Do you want watchdog to act like a real-time application
 * (i.e. lock its pages in memory)?
 */

#define REALTIME

/*
 * Now some check specific defines.
 *
 * Maximal 1 min load average.
 */

/* #define MAXLOAD 12 */

/*
 * Minimal 1 min load average, the lowest value that is accepted as a maxload parameter.
 */
 
/* #define DMINLOAD 2 */

/*
 * Maximal temperature (make sure you use the same unit as
 * your watchdog hardware).
 */
 
/* #define MAXTEMP 120 */

/*
 * how long are the lines in our config file
 */
 
/* #define CONFIG_LINE_LEN 80 */

/*
 * The next parameters define the files to be accessed.
 * You shouldn´t need to change any of these. The values listed below
 * are the defaults.
 *
 * What's the name of your watchdog device?
 * Leave DEV empty to disable keep alive support per default.
 */
 
#define DEVNAME "/dev/watchdog"

/*
 * What's the name of your temperature device?
 * Leave TEMP empty to disable temperature checking per default.
 */
  
#define TEMPNAME "/dev/temperature"

/*
 * name of the PID file
 */
 
/* #define PIDFILE "/var/run/watchdog.pid" */

/*
 * where do we save the random seed, set to "" to disable
 */

/* #define RANDOM_SEED "/var/run/random-seed" */

/*
 * where is our config file
 */

/* #define CONFIG_FILENAME "/etc/watchdog.conf" */

/*
 * And some system specific defines. Should be okay on any system.
 *
 * Kernel timer margin.
 */
 
/* #define TIMER_MARGIN 60 */

/*
 * Where is your sendmail binary (default is _PATH_SENDMAIL).
 */

/* #define PATH_SENDMAIL "/usr/sbin/sendmail" */ 

/*
 * Which priority should watchdog use when scheduled as real-time application?
 */

/* #define SCHEDULE_PRIORITY 1 */

/*
 * For the code taken from mount.
 */
#define HAVE_NFS
#define FSTYPE_DEFAULT "iso9660"

/*
 * Version number. Do not edit!
 */

#define MAJOR_VERSION 4
#define MINOR_VERSION 1
