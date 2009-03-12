/*
 * A umount(8) for Linux 0.99.
 * umount.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 *
 * Wed Sep 14 22:43:54 1994: Sebastian Lederer
 * (lederer@next-pc.informatik.uni-bonn.de) added support for sending an
 * unmount RPC call to the server when an NFS-filesystem is unmounted.
 *
 * Tue Sep 26 16:33:09 1995: Added patches from Greg Page (greg@caldera.com)
 * so that NetWare filesystems can be unmounted.
 *
 * 951213: Marek Michalkiewicz <marekm@i17linuxb.ists.pwr.wroc.pl>:
 * Ignore any RPC errors, so that you can umount an nfs mounted filesystem
 * if the server is down.
 *
 * 960223: aeb - several minor changes
 * 960324: aeb - added some changes from Rob Leslie <rob@mars.org>
 * 960823: aeb - also try umount(spec) when umount(node) fails
 * 970307: aeb - canonise names from fstab
 */

#include <unistd.h>
#include <sys/stat.h>
#include "sundries.h"
#include "lomount.h"
#include "loop.h"

#ifdef HAVE_NFS
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_prot.h>
#include "nfsmount.h"
#include <arpa/inet.h>
#endif

#if 0 /* WATCHDOG */
#ifdef notyet
/* Nonzero for force umount (-f).  This needs kernel support we don't have.  */
int force = 0;
#endif

/* Don't write a entry in /etc/mtab (-n).  */
int nomtab = 0;

/* Nonzero for chatty (-v).  This is a nonstandard flag (not in BSD).  */
int verbose = 0;

/* True if ruid != euid.  */
int suid = 0;
#else /* WATCHDOG */
static int nomtab = 0;
static int verbose = 0;
#endif /* WATCHDOG */

#ifdef HAVE_NFS
static int xdr_dir(XDR *xdrsp, char *dirp)
{
      return (xdr_string(xdrsp, &dirp, MNTPATHLEN));
}

static int
nfs_umount_rpc_call(const char *spec, const char *opts)
{
      register CLIENT *clp;
      struct sockaddr_in saddr;
      struct timeval pertry, try;
      enum clnt_stat clnt_stat;
      int so = RPC_ANYSOCK;
      struct hostent *hostp;
      char *hostname;
      char *dirname;
      char *p;

      if (spec == NULL || (p = strchr(spec,':')) == NULL)
		return 0;
      hostname = xstrndup(spec, p-spec);
      dirname = xstrdup(p+1);
#ifdef DEBUG
      printf("host: %s, directory: %s\n", hostname, dirname);
#endif

      if (opts && (p = strstr(opts, "addr="))) {
	   char *q;

	   free(hostname);
	   p += 5;
	   q = p;
	   while (*q && *q != ',') q++;
	   hostname = xstrndup(p,q-p);
      }

      if (hostname[0] >= '0' && hostname[0] <= '9')
	   saddr.sin_addr.s_addr = inet_addr(hostname);
      else
	   if ((hostp = gethostbyname(hostname)) == NULL) {
		fprintf(stderr, "mount: can't get address for %s\n",
			hostname);
		return 1;
	   } else
		memcpy(&saddr.sin_addr, hostp->h_addr, hostp->h_length);

      saddr.sin_family = AF_INET;
      saddr.sin_port = 0;
      pertry.tv_sec = 3;
      pertry.tv_usec = 0;
      if ((clp = clntudp_create(&saddr, MOUNTPROG, MOUNTVERS,
				pertry, &so)) == NULL) {
	   clnt_pcreateerror("Cannot MOUNTPROG RPC");
	   return (1);
      }
      clp->cl_auth = authunix_create_default();
      try.tv_sec = 20;
      try.tv_usec = 0;
      clnt_stat = clnt_call(clp, MOUNTPROC_UMNT,
			    (xdrproc_t) xdr_dir, dirname,
			    (xdrproc_t) xdr_void, (caddr_t) 0,
			    try);

      if (clnt_stat != RPC_SUCCESS) {
	   clnt_perror(clp, "Bad UMNT RPC");
	   return (1);
      }
      auth_destroy(clp->cl_auth);
      clnt_destroy(clp);

      return (0);
}
#endif /* HAVE_NFS */

/* complain about a failed umount */
static void complain(int err, const char *dev) {
  switch (err) {
    case ENXIO:
      error ("umount: %s: invalid block device", dev); break;
    case EINVAL:
      error ("umount: %s: not mounted", dev); break;
    case EIO:
      error ("umount: %s: can't write superblock", dev); break;
    case EBUSY:
     /* Let us hope fstab has a line "proc /proc ..."
	and not "none /proc ..."*/
      error ("umount: %s: device is busy", dev); break;
    case ENOENT:
      error ("umount: %s: not found", dev); break;
    case EPERM:
      error ("umount: %s: must be superuser to umount", dev); break;
    case EACCES:
      error ("umount: %s: block devices not permitted on fs", dev); break;
    default:
      error ("umount: %s: %s", dev, strerror (err)); break;
  }
}

/* Umount a single device.  Return a status code, so don't exit
   on a non-fatal error.  We lock/unlock around each umount.  */
static int
umount_one (const char *spec, const char *node, const char *type,
	    const char *opts)
{
  int umnt_err, umnt_err2;
  int isroot;
  int res;
  struct mntent *mnt;


  /* Special case for root.  As of 0.99pl10 we can (almost) unmount root;
     the kernel will remount it readonly so that we can carry on running
     afterwards.  The readonly remount is illegal if any files are opened
     for writing at the time, so we can't update mtab for an unmount of
     root.  As it is only really a remount, this doesn't matter too
     much.  [sct May 29, 1993] */
  isroot = (streq (node, "/") || streq (node, "root")
	                      || streq (node, "rootfs"));
  if (isroot)
    nomtab++;

#ifdef HAVE_NFS
  /* Ignore any RPC errors, so that you can umount the filesystem
     if the server is down.  */
  if (strcasecmp(type, "nfs") == 0)
	  nfs_umount_rpc_call(spec, opts);
#endif
 
  if (!nomtab)
	  lock_mtab ();


  umnt_err = umnt_err2 = 0;
  res = umount (node);
  if (res < 0) {
       umnt_err = errno;
       /* A device might have been mounted on a node that has since
	  been deleted or renamed, so if node fails, also try spec. */
       /* if (umnt_err == ENOENT || umnt_err == EINVAL) */
       if (umnt_err != EBUSY && strcmp(node, spec)) {
	    if (verbose)
		 printf ("could not umount %s - trying %s instead\n",
			 node, spec);
	    res = umount (spec);
	    if (res < 0)
		 umnt_err2 = errno;
	    /* Do not complain about remote NFS mount points */
	    if (errno == ENOENT && index(spec, ':'))
		 umnt_err2 = 0;
       }
  }

  if (res >= 0) {
      /* Umount succeeded, update mtab.  */
      if (verbose)
	printf ("%s umounted\n", spec);

      if (!nomtab) {
				/* Special stuff for loop devices */
	  open_mtab("r");
	  if ((mnt = getmntfile (spec)) ||
	      (mnt = getmntfile (node))) {
	     char *opts;

	     /* old style mtab line? */
	     if (streq(mnt->mnt_type, "loop"))
		if (del_loop(spec))
		      goto fail;

	     /* new style mtab line? */
	     opts = mnt->mnt_opts ? xstrdup(mnt->mnt_opts) : "";
	     for (opts = strtok (opts, ","); opts; opts = strtok (NULL, ",")) {
		 if (!strncmp(opts, "loop=", 5)) {
		     if (del_loop(opts+5))
		       goto fail;
		     break;
		 }
	     }
	  } else {
	      /* maybe spec is a loop device? */
	      /* no del_loop() - just delete it from mtab */
	      if ((mnt = getmntoptfile (spec)) != NULL)
		node = mnt->mnt_dir;
	  }
	  close_mtab();

				/* Non-loop stuff */
	  update_mtab (node, NULL);
	  unlock_mtab ();
      }
      return 0;
  }

fail:
  /* Umount or del_loop failed, complain, but don't die.  */
  if (!nomtab) {
      /* remove obsolete entry */
      if (umnt_err == EINVAL || umnt_err == ENOENT)
	  update_mtab (node, NULL);
      unlock_mtab ();
  }

  if (umnt_err2)
       complain(umnt_err2, spec);
  if (umnt_err && umnt_err != umnt_err2)
       complain(umnt_err, node);
  return 1;
}

/* Unmount all filesystems of type VFSTYPES found in mtab.  Since we are
   concurrently updating mtab after every succesful umount, we have to
   slurp in the entire file before we start.  This isn't too bad, because
   in any case it's important to umount mtab entries in reverse order
   to mount, e.g. /usr/spool before /usr.  */
#if 0 /* WATCHDOG */
static
#endif /* WATCHDOG */
int
umount_all (string_list types)
{
  string_list spec_list = NULL;
  string_list node_list = NULL;
  string_list type_list = NULL;
  string_list opts_list = NULL;
  struct mntent *mnt;
  int errors;

  open_mtab ("r");

  while ((mnt = getmntent (F_mtab)))
    if (matching_type (mnt->mnt_type, types))
      {
	spec_list = cons (xstrdup (mnt->mnt_fsname), spec_list);
	node_list = cons (xstrdup (mnt->mnt_dir), node_list);
        type_list = cons (xstrdup (mnt->mnt_type), type_list);
	opts_list = cons (xstrdup (mnt->mnt_opts), opts_list);
      }

  close_mtab ();

  errors = 0;
  while (spec_list != NULL)
    {
      errors |= umount_one (car (spec_list), car (node_list),
			    car (type_list), car (opts_list));
      spec_list = cdr (spec_list);
      node_list = cdr (node_list);
      type_list = cdr (type_list);
      opts_list = cdr (opts_list);
    }

  sync ();
  return errors;
}

#if 0 /* WATCHDOG */
extern char version[];
static struct option longopts[] =
{
  { "all", 0, 0, 'a' },
  { "force", 0, 0, 'f' },
  { "help", 0, 0, 'h' },
  { "no-mtab", 0, 0, 'n' },
  { "verbose", 0, 0, 'v' },
  { "version", 0, 0, 'V' },
  { "types", 1, 0, 't' },
  { NULL, 0, 0, 0 }
};

char *usage_string = "\
usage: umount [-hV]\n\
       umount -a [-n] [-v] [-t vfstypes]\n\
       umount [-n] [-v] special | node...\n\
";

static void
usage (FILE *fp, int n)
{
  fprintf (fp, "%s", usage_string);
  exit (n);
}

int mount_quiet = 0;

int
main (int argc, char *argv[])
{
  int c;
  int all = 0;
  string_list types = NULL;
  string_list options;
  struct mntent *mnt;
  struct mntent mntbuf;
  struct mntent *fs;
  char *file;
  int result = 0;

  while ((c = getopt_long (argc, argv, "afhnvVt:", longopts, NULL)) != EOF)
    switch (c) {
      case 'a':			/* umount everything */
	++all;
	break;
      case 'f':			/* force umount (needs kernel support) */
#if 0
	++force;
#else
	die (2, "umount: forced umount not supported yet");
#endif
	break;
      case 'h':			/* help */
	usage (stdout, 0);
	break;
      case 'n':
	++nomtab;
	break;
      case 'v':			/* make noise */
	++verbose;
	break;
      case 'V':			/* version */
	printf ("umount: %s\n", version);
	exit (0);
      case 't':			/* specify file system type */
	types = parse_list (optarg);
	break;
      case 0:
	break;
      case '?':
      default:
	usage (stderr, 1);
    }

  if (getuid () != geteuid ())
    {
      suid = 1;
      if (all || types || nomtab)
	die (2, "umount: only root can do that");
    }

  argc -= optind;
  argv += optind;

  if (all)
    result = umount_all (types);
  else if (argc < 1)
    usage (stderr, 2);
  else while (argc--)
    {
      file = canonicalize (*argv); /* mtab paths are canonicalized */
      if (verbose > 1)
	printf("Trying to umount %s\n", file);

      open_mtab ("r");
      mnt = getmntfile (file);
      if (mnt)
	{
	  /* Copy the structure and strings because they're in static areas. */
	  mntbuf = *mnt;
	  mnt = &mntbuf;
	  mnt->mnt_fsname = xstrdup (mnt->mnt_fsname);
	  mnt->mnt_dir = xstrdup (mnt->mnt_dir);
	}
      else if (verbose)
	printf("Could not find %s in mtab\n", file);
      close_mtab ();

      if (suid)
	{
	  if (!mnt)
	    die (2, "umount: %s is not mounted (according to mtab)", file);
	  if (!(fs = getfsspec (file)) && !(fs = getfsfile (file)))
	    die (2, "umount: %s is not in the fstab (and you are not root)",
		 file);
	  if ((!streq (mnt->mnt_fsname, fs->mnt_fsname) &&
	       !streq (mnt->mnt_fsname, canonicalize (fs->mnt_fsname)))
	      || (!streq (mnt->mnt_dir, fs->mnt_dir) &&
		  !streq (mnt->mnt_dir, canonicalize (fs->mnt_dir)))) {
	    die (2, "umount: %s mount disagrees with the fstab", file);
	  }
	  options = parse_list (fs->mnt_opts);
	  while (options)
	    {	
	      if (streq (car (options), "user"))
		break;
	      options = cdr (options);
	    }
	  if (!options)
	    die (2, "umount: only root can unmount %s from %s",
		 fs->mnt_fsname, fs->mnt_dir);
	}

      if (mnt)
	 result = umount_one (xstrdup(mnt->mnt_fsname), xstrdup(mnt->mnt_dir),
			      xstrdup(mnt->mnt_type), xstrdup(mnt->mnt_opts));
      else
	 result = umount_one (*argv, *argv, *argv, *argv);

      argv++;

    }
  exit (result);
}

#endif /* WATCHDOG */
