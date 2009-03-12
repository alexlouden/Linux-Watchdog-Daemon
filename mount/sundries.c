/*
 * Support functions.  Exported functions are prototyped in sundries.h.
 * sundries.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 *
 * added fcntl locking by Kjetil T. (kjetilho@math.uio.no) - aeb, 950927
 */
#include <unistd.h>
#include <string.h>
#include <setjmp.h>
#include "sundries.h"
#include "nfsmount.h"

extern jmp_buf ret2dog;

/* File pointer for /etc/mtab.  */
FILE *F_mtab = NULL;

/* File pointer for temp mtab.  */
FILE *F_temp = NULL;

/* File descriptor for lock.  Value tested in unlock_mtab() to remove race.  */
static int lock = -1;

/* Flag for already existing lock file. */
static int old_lockfile = 1;

/* String list constructor.  (car() and cdr() are defined in "sundries.h").  */
string_list
cons (char *a, const string_list b)
{
  string_list p;

  p = xmalloc (sizeof *p);

  car (p) = a;
  cdr (p) = b;
  return p;
}

void *
xmalloc (size_t size)
{
  void *t;

  if (size == 0)
    return NULL;

  t = malloc (size);
  if (t == NULL)
    die (2, "not enough memory");
  
  return t;
}

char *
xstrdup (const char *s)
{
  char *t;

  if (s == NULL)
    return NULL;
 
  t = strdup (s);

  if (t == NULL)
    die (2, "not enough memory");

  return t;
}

char *
xstrndup (const char *s, int n)
{
  char *t;

  if (s == NULL)
    die (2, "bug in xstrndup call");

  t = xmalloc(n+1);
  strncpy(t,s,n);
  t[n] = 0;

  return t;
}

/* Call this with SIG_BLOCK to block and SIG_UNBLOCK to unblock.  */
void
block_signals (int how)
{
  sigset_t sigs;

  sigfillset (&sigs);
  sigdelset(&sigs, SIGTRAP);
  sigdelset(&sigs, SIGSEGV);
  sigprocmask (how, &sigs, (sigset_t *) 0);
}


/* Non-fatal error.  Print message and return.  */
void
error (const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  fprintf (stderr, "\n");
  va_end (args);
}

/* Fatal error.  Print message and exit.  */
void
die (int err, const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  fprintf (stderr, "\n");
  va_end (args);

  unlock_mtab ();
  longjmp(ret2dog, err);
}

/* Ensure that the lock is released if we are interrupted.  */
static void
handler (int sig)
{
  die (2, "%s", sys_siglist[sig]);
}

static void
setlkw_timeout (int sig)
{
  /* nothing, fcntl will fail anyway */
}

/* Create the lock file.  The lock file will be removed if we catch a signal
   or when we exit.  The value of lock is tested to remove the race.  */
void
lock_mtab (void)
{
  int sig = 0;
  struct sigaction sa;
  struct flock flock;

  /* If this is the first time, ensure that the lock will be removed.  */
  if (lock < 0)
    {
      struct stat st;
      sa.sa_handler = handler;
      sa.sa_flags = 0;
      sigfillset (&sa.sa_mask);
  
      while (sigismember (&sa.sa_mask, ++sig) != -1)
	{
	  if (sig == SIGALRM)
	    sa.sa_handler = setlkw_timeout;
	  else
	    sa.sa_handler = handler;
	  sigaction (sig, &sa, (struct sigaction *) 0);
	}

      /* This stat is performed so we know when not to be overly eager
	 when cleaning up after signals. The window between stat and
	 open is not significant. */
      if (lstat (MOUNTED_LOCK, &st) < 0 && errno == ENOENT)
	old_lockfile = 0;

      lock = open (MOUNTED_LOCK, O_WRONLY|O_CREAT, 0);
      if (lock < 0)
        {
          die (2, "can't create lock file %s: %s (use -n flag to override)",
	       MOUNTED_LOCK, strerror (errno));
        }

      flock.l_type = F_WRLCK;
      flock.l_whence = SEEK_SET;
      flock.l_start = 0;
      flock.l_len = 0;

      alarm(LOCK_BUSY);
      if (fcntl (lock, F_SETLKW, &flock) < 0)
	{
	  close (lock);
	  /* The file should not be removed */
	  lock = -1;
	  die (2, "can't lock lock file %s: %s",
	       MOUNTED_LOCK, errno == EINTR ? "timed out" : strerror (errno));
	}
      /* We have now access to the lock, and it can always be removed */
      old_lockfile = 0;
    }
}

/* Remove lock file.  */
void
unlock_mtab (void)
{
  if (lock != -1)
    {
      close (lock);
      if (!old_lockfile)
	unlink (MOUNTED_LOCK);
    }
}

/* Open mtab.  */
void
open_mtab (const char *mode)
{
  if ((F_mtab = setmntent (MOUNTED, mode)) == NULL)
    die (2, "can't open %s: %s", MOUNTED, strerror (errno));
}

/*
 * Close mtab.
 * Either don't do a chmod for a read-only fs or don't complain if it fails.
 */
void
close_mtab (void)
{
  if (fchmod (fileno (F_mtab), S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) < 0)
    if (errno != EROFS)
      die (1, "mount: error changing mode of %s: %s",
	   MOUNTED, strerror (errno));
  endmntent (F_mtab);
}

/*
 * Update the mtab.
 *  Used by umount with null INSTEAD: remove any DIR entries.
 *  Used by mount upon a remount: update option part,
 *   and complain if a wrong device or type was given.
 *   [Note that often a remount will be a rw remount of /
 *    where there was no entry before, and we'll have to believe
 *    the values given in INSTEAD.]
 */

void
update_mtab (const char *dir, struct mntent *instead)
{
  struct mntent *mnt;
  struct mntent *next;
  struct mntent remnt;
  int added = 0;

  open_mtab ("r");

  if ((F_temp = setmntent (MOUNTED_TEMP, "w")) == NULL)
      die (2, "can't open %s: %s", MOUNTED_TEMP, strerror (errno));
  
  while ((mnt = getmntent (F_mtab))) {
      if (streq (mnt->mnt_dir, dir)) {
          added++;
	  if (instead) {	/* a remount */
	      remnt = *instead;
	      next = &remnt;
	      remnt.mnt_fsname = mnt->mnt_fsname;
	      remnt.mnt_type = mnt->mnt_type;
	      if (instead->mnt_fsname
		  && !streq(mnt->mnt_fsname, instead->mnt_fsname))
		   printf("mount: warning: "
			  "cannot change mounted device with a remount\n");
	      else if (instead->mnt_type
		       && !streq(instead->mnt_type, "unknown")
		       && !streq(mnt->mnt_type, instead->mnt_type))
		   printf("mount: warning: "
			  "cannot change filesystem type with a remount\n");
	  } else
	      next = NULL;
      } else
          next = mnt;
      if (next && addmntent(F_temp, next) == 1)
	  die (1, "error writing %s: %s", MOUNTED_TEMP, strerror (errno));
  }
  if (instead && !added)
      addmntent(F_temp, instead);

  endmntent (F_mtab);
  if (fchmod (fileno (F_temp), S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) < 0)
      die (1, "error changing mode of %s: %s", MOUNTED_TEMP, strerror (errno));
  endmntent (F_temp);

  if (rename (MOUNTED_TEMP, MOUNTED) < 0)
      die (1, "can't rename %s to %s: %s",
	   MOUNTED_TEMP, MOUNTED, strerror(errno));
}

/* Given the name NAME, try to find it in file FP.  */
struct mntent *
getmntfromfile (const char *name, FILE *fp) {
    struct mntent *mnt;

    if (!fp)
        return NULL;

    rewind(fp);

    while ((mnt = getmntent (fp)) != NULL) {
        if (streq (mnt->mnt_dir, name))
	    break;
        if (streq (mnt->mnt_fsname, name))
	    break;
    }

    return mnt;
}

/* Given the name NAME, try to find it in mtab.  */ 
struct mntent *
getmntfile (const char *name) {
    return getmntfromfile (name, F_mtab);
}

/* Given the name FILE, try to find the option "loop=FILE" in mtab.  */ 
struct mntent *
getmntoptfile (const char *file)
{
  struct mntent *mnt;
  char *opts, *s;
  int l;

  if (!F_mtab || !file)
     return NULL;

  l = strlen(file);

  rewind(F_mtab);

  while ((mnt = getmntent (F_mtab)) != NULL)
    if ((opts = mnt->mnt_opts)
	&& (s = strstr(opts, "loop="))
	&& !strncmp(s+5, file, l)
	&& (s == opts || s[-1] == ',')
	&& (s[l+5] == 0 || s[l+5] == ','))
      return mnt;

  return NULL;
}

/* Parse a list of strings like str[,str]... into a string list.  */
string_list
parse_list (char *strings)
{
  string_list list;
  char *t;

  if (strings == NULL)
    return NULL;

  list = cons (strtok (strings, ","), NULL);

  while ((t = strtok (NULL, ",")) != NULL)
    list = cons (t, list);

  return list;
}

/* True if fstypes match.  Null *TYPES means match anything,
   except that swap types always return false.
   This routine has some ugliness to deal with ``no'' types.
   Fixed bug: the `no' part comes at the end - aeb, 970216  */
int
matching_type (const char *type, string_list types)
{
  char *notype;
  int foundyes, foundno;
  int no;			/* true if a "no" type match, eg -t nominix */

  if (streq (type, MNTTYPE_SWAP))
    return 0;
  if (types == NULL)
    return 1;

  if ((notype = alloca (strlen (type) + 3)) == NULL)
    die (2, "mount: out of memory");
  sprintf (notype, "no%s", type);

  foundyes = foundno = no = 0;
  while (types != NULL) {
    if (cdr (types) == NULL)
	 no = (car (types)[0] == 'n') && (car (types)[1] == 'o');
    if (streq (type, car (types)))
      foundyes = 1;
    else if (streq (notype, car (types)))
      foundno = 1;
    types = cdr (types);
  }

  return (foundno ? 0 : (no ^ foundyes));
}

/* Make a canonical pathname from PATH.  Returns a freshly malloced string.
   It is up the *caller* to ensure that the PATH is sensible.  i.e.
   canonicalize ("/dev/fd0/.") returns "/dev/fd0" even though ``/dev/fd0/.''
   is not a legal pathname for ``/dev/fd0''.  Anything we cannot parse
   we return unmodified.   */
char *
canonicalize (const char *path)
{
  char *canonical;
  
  if (path == NULL)
    return NULL;

  if (streq(path, "none") || streq(path, "proc"))
    return xstrdup(path);

  canonical = xmalloc (PATH_MAX + 1);
  
  if (realpath (path, canonical))
    return canonical;

  free(canonical);
  return xstrdup(path);
}
