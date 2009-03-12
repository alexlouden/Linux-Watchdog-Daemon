/*
 * A mount(8) for Linux 0.99.
 * mount.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 *
 * Thu Jul 14 07:32:40 1994: faith@cs.unc.edu added changes from Adam
 * J. Richter (adam@adam.yggdrasil.com) so that /proc/filesystems is used
 * if no -t option is given.  I modified his patches so that, if
 * /proc/filesystems is not available, the behavior of mount is the same as
 * it was previously.
 *
 * Wed Sep 14 22:43:00 1994: Mitchum DSouza
 * (mitch@mrc-applied-psychology.cambridge.ac.uk) added support for mounting
 * the "loop" device.
 *
 * Wed Sep 14 22:55:10 1994: Sander van Malssen (svm@kozmix.hacktic.nl)
 * added support for remounting readonly file systems readonly.
 *
 * Wed Feb 8 09:23:18 1995: Mike Grupenhoff <kashmir@umiacs.UMD.EDU> added
 * a probe of the superblock for the type before /proc/filesystems is
 * checked.
 *
 * Wed Feb  8 12:27:00 1995: Andries.Brouwer@cwi.nl fixed up error messages.
 * Sat Jun  3 20:44:38 1995: Patches from Andries.Brouwer@cwi.nl applied.
 * Tue Sep 26 22:38:20 1995: aeb@cwi.nl, many changes
 * Fri Feb 23 13:47:00 1996: aeb@cwi.nl, loop device related changes
 *
 * Fri Apr  5 01:13:33 1996: quinlan@bucknell.edu, fixed up iso9660 autodetect
 */

#include <unistd.h>
#include <ctype.h>

#include <sys/ioctl.h>
#include <sys/stat.h>

#if !defined(__GLIBC__)
#define _LINUX_TYPES_H		/* kludge to prevent inclusion */
#endif /* __GLIBC */
#include <linux/fs.h>

#if defined(sparc)
#include <asm-sparc/types.h>
#endif

#include <linux/minix_fs.h>
#include <linux/ext2_fs.h>
#include <linux/iso_fs.h>

#if defined(__GLIBC__)
#define _SOCKETBITS_H
typedef unsigned int socklen_t;
#endif /* __GLIBC */
#include "sundries.h"
#include "lomount.h"
#include "loop.h"

#define PROC_FILESYSTEMS	"/proc/filesystems"
#define SIZE(a) (sizeof(a)/sizeof(a[0]))

#if 0 /* WATCHDOG */
/* True for fake mount (-f).  */
int fake = 0;

/* Don't write a entry in /etc/mtab (-n).  */
int nomtab = 0; 

/* True for readonly (-r).  */
int readonly = 0;

/* Nonzero for chatty (-v).  */
int verbose = 0;

/* True for read/write (-w).  */
int readwrite = 0;

/* True for all mount (-a).  */
int all = 0;

/* True if ruid != euid.  */
int suid = 0;
#else /* WATCHDOG */
static int readonly = 0;
static int suid = 0;
int verbose = 0;
static int readwrite = 0;
static int all = 0;
static int nomtab = 1;
static int fake = 0;
#endif /* WATCHDOG */

/* Map from -o and fstab option strings to the flag argument to mount(2).  */
struct opt_map
{
  const char *opt;		/* option name */
  int  skip;			/* skip in mtab option string */
  int  inv;			/* true if flag value should be inverted */
  int  mask;			/* flag mask value */
};

/* Custom mount options for our own purposes.  */
/* We can use the high-order 16 bits, since the mount call
   has MS_MGC_VAL there. */
#define MS_NOAUTO	0x80000000
#define MS_USER		0x40000000
#define MS_LOOP		0x00010000

/* Options that we keep the mount system call from seeing.  */
#define MS_NOSYS	(MS_NOAUTO|MS_USER|MS_LOOP)

/* Options that we keep from appearing in the options field in the mtab.  */
#define MS_NOMTAB	(MS_REMOUNT|MS_NOAUTO|MS_USER)

/* OPTIONS that we make ordinary users have by default.  */
#define MS_SECURE	(MS_NOEXEC|MS_NOSUID|MS_NODEV)

const struct opt_map opt_map[] = {
  { "defaults",	0, 0, 0		},	/* default options */
  { "ro",	1, 0, MS_RDONLY	},	/* read-only */
  { "rw",	1, 1, MS_RDONLY	},	/* read-write */
  { "exec",	0, 1, MS_NOEXEC	},	/* permit execution of binaries */
  { "noexec",	0, 0, MS_NOEXEC	},	/* don't execute binaries */
  { "suid",	0, 1, MS_NOSUID	},	/* honor suid executables */
  { "nosuid",	0, 0, MS_NOSUID	},	/* don't honor suid executables */
  { "dev",	0, 1, MS_NODEV	},	/* interpret device files  */
  { "nodev",	0, 0, MS_NODEV	},	/* don't interpret devices */
  { "sync",	0, 0, MS_SYNCHRONOUS},	/* synchronous I/O */
  { "async",	0, 1, MS_SYNCHRONOUS},	/* asynchronous I/O */
  { "remount",  0, 0, MS_REMOUNT},      /* Alter flags of mounted FS */
  { "auto",	0, 1, MS_NOAUTO	},	/* Can be mounted using -a */
  { "noauto",	0, 0, MS_NOAUTO	},	/* Can  only be mounted explicitly */
  { "user",	0, 0, MS_USER	},	/* Allow ordinary user to mount */
  { "nouser",	0, 1, MS_USER	},	/* Forbid ordinary user to mount */
  /* add new options here */
#ifdef MS_NOSUB
  { "sub",	0, 1, MS_NOSUB	},	/* allow submounts */
  { "nosub",	0, 0, MS_NOSUB	},	/* don't allow submounts */
#endif
#ifdef MS_SILENT
  { "quiet",	0, 0, MS_SILENT    },	/* be quiet  */
  { "loud",	0, 1, MS_SILENT    },	/* print out messages. */
#endif
#ifdef MS_MANDLOCK
  { "mand",	0, 0, MS_MANDLOCK },	/* Allow mandatory locks on this FS */
  { "nomand",	0, 1, MS_MANDLOCK },	/* Forbid mandatory locks on this FS */
#endif
  { "loop",	1, 0, MS_LOOP	},	/* use a loop device */
#ifdef MS_NOATIME
  { "atime",	0, 1, MS_NOATIME },	/* Update access time */
  { "noatime",	0, 0, MS_NOATIME },	/* Do not update access time */
#endif
  { NULL,	0, 0, 0		}
};

char *opt_loopdev, *opt_vfstype, *opt_offset, *opt_encryption;

struct string_opt_map {
  char *tag;
  int skip;
  char **valptr;
} string_opt_map[] = {
  { "loop=",	0, &opt_loopdev },
  { "vfs=",	1, &opt_vfstype },
  { "offset=",	0, &opt_offset },
  { "encryption=", 0, &opt_encryption },
  { NULL, 0, NULL }
};

static void
clear_string_opts(void) {
  struct string_opt_map *m;

  for (m = &string_opt_map[0]; m->tag; m++)
    *(m->valptr) = NULL;
}

static int
parse_string_opt(char *s) {
  struct string_opt_map *m;
  int lth;

  for (m = &string_opt_map[0]; m->tag; m++) {
    lth = strlen(m->tag);
    if (!strncmp(s, m->tag, lth)) {
      *(m->valptr) = xstrdup(s + lth);
      return 1;
    }
  }
  return 0;
}

#if 0 /* WATCHDOG */
int mount_quiet = 0;
#else
static int mount_quiet = 0;
#endif /* WATCHDOG */

/* Report on a single mount.  */
static void
print_one (const struct mntent *mnt)
{
  if (mount_quiet) return;
  printf ("%s on %s", mnt->mnt_fsname, mnt->mnt_dir);
  if ((mnt->mnt_type != NULL) && *mnt->mnt_type != '\0')
    printf (" type %s", mnt->mnt_type);
  if (mnt->mnt_opts != NULL)
    printf (" (%s)", mnt->mnt_opts);
  printf ("\n");
}

#if 0 /* WATCHDOG */
/* Report on everything in mtab (of the specified types if any).  */
static int
print_all (string_list types)
{
  struct mntent *mnt;
  
  open_mtab ("r");

  while ((mnt = getmntent (F_mtab)) != NULL)
    if (matching_type (mnt->mnt_type, types))
      print_one (mnt);

  if (ferror (F_mtab))
    die (1, "mount: error reading %s: %s", MOUNTED, strerror (errno));

  exit (0);
}
#endif /* WATCHDOG */

/* Look for OPT in opt_map table and return mask value.  If OPT isn't found,
   tack it onto extra_opts (which is non-NULL).  */
static inline void
parse_opt (const char *opt, int *mask, char *extra_opts)
{
  const struct opt_map *om;

  for (om = opt_map; om->opt != NULL; om++)
    if (streq (opt, om->opt))
      {
	if (om->inv)
	  *mask &= ~om->mask;
	else
	  *mask |= om->mask;
	if (om->mask == MS_USER)
	  *mask |= MS_SECURE;
#ifdef MS_SILENT
        if (om->mask == MS_SILENT && om->inv)  {
          mount_quiet = 1;
          verbose = 0;
        }
#endif
	return;
      }
  if (*extra_opts)
    strcat(extra_opts, ",");
  strcat(extra_opts, opt);
}
  
/* Take -o options list and compute 4th and 5th args to mount(2).  flags
   gets the standard options and extra_opts anything we don't recognize.  */
static void
parse_opts (char *opts, int *flags, char **extra_opts)
{
  char *opt;

  *flags = 0;
  *extra_opts = NULL;

  clear_string_opts();

  if (opts != NULL)
    {
      *extra_opts = xmalloc (strlen (opts) + 1); 
      **extra_opts = '\0';

      for (opt = strtok (opts, ","); opt; opt = strtok (NULL, ","))
	if (!parse_string_opt (opt))
	  parse_opt (opt, flags, *extra_opts);
    }

  if (readonly)
    *flags |= MS_RDONLY;
  if (readwrite)
    *flags &= ~MS_RDONLY;
}

/* Try to build a canonical options string.  */
static char *
fix_opts_string (int flags, char *extra_opts)
{
  const struct opt_map *om;
  const struct string_opt_map *m;
  char *new_opts;
  char *tmp;

  new_opts = (flags & MS_RDONLY) ? "ro" : "rw";
  for (om = opt_map; om->opt != NULL; om++) {
      if (om->skip)
	continue;
      if (om->inv || !om->mask || (flags & om->mask) != om->mask)
	continue;
      tmp = xmalloc(strlen(new_opts) + strlen(om->opt) + 2);
      sprintf(tmp, "%s,%s", new_opts, om->opt);
      new_opts = tmp;
      flags &= ~om->mask;
  }
  for (m = &string_opt_map[0]; m->tag; m++) {
      if (!m->skip && *(m->valptr)) {
	  tmp = xmalloc(strlen(new_opts) + strlen(m->tag) + strlen(*(m->valptr)) + 2);
	  sprintf(tmp, "%s,%s%s", new_opts, m->tag, *(m->valptr));
	  new_opts = tmp;
      }
  }
  if (extra_opts && *extra_opts) {
      tmp = xmalloc(strlen(new_opts) + strlen(extra_opts) + 2);
      sprintf(tmp, "%s,%s", new_opts, extra_opts);
      new_opts = tmp;
  }
  return new_opts;
}

/* Most file system types can be recognized by a `magic' number
   in the superblock.  Note that the order of the tests is
   significant: by coincidence a filesystem can have the
   magic numbers for several file system types simultaneously.
   For example, the romfs magic lives in the 1st sector;
   xiafs does not touch the 1st sector and has its magic in
   the 2nd sector; ext2 does not touch the first two sectors. */

/* ext definitions - required since 2.1.21 */
#ifndef EXT_SUPER_MAGIC
#define EXT_SUPER_MAGIC 0x137D
struct ext_super_block {
	unsigned long s_dummy[14];
	unsigned short s_magic;
};
#endif

/* xiafs definitions - required since they were removed from
   the kernel since 2.1.21 */
#ifndef _XIAFS_SUPER_MAGIC
#define _XIAFS_SUPER_MAGIC 0x012FD16D
struct xiafs_super_block {
    u_char  s_boot_segment[512];	/*  1st sector reserved for boot */
    u_long  s_dummy[15];
    u_long  s_magic;			/* 15: magic number for xiafs    */
};
#endif

#ifndef EXT2_PRE_02B_MAGIC
#define EXT2_PRE_02B_MAGIC    0xEF51
#endif

static inline unsigned short
swapped(unsigned short a) {
     return (a>>8) | (a<<8);
}

/*
    char *fstype(const char *device);

    Probes the device and attempts to determine the type of filesystem
    contained within.

    Original routine by <jmorriso@bogomips.ww.ubc.ca>; made into a function
    for mount(8) by Mike Grupenhoff <kashmir@umiacs.umd.edu>.
    Read the superblock only once - aeb
    Added a test for iso9660 - aeb
    Added a test for high sierra (iso9660) - quinlan@bucknell.edu
    Corrected the test for xiafs - aeb
    added romfs - aeb

    Currently supports: minix, ext, ext2, xiafs, iso9660, romfs
*/
char *magic_known[] = { "minix", "ext", "ext2", "xiafs", "iso9660", "romfs" };

static int
tested(const char *device) {
    char **m;

    for (m = magic_known; m - magic_known < SIZE(magic_known); m++)
        if (!strcmp(*m, device))
	    return 1;
    return 0;
}

static char *
fstype(const char *device)
{
    int fd;
    char *type = NULL;
    union {
	struct minix_super_block ms;
	struct ext_super_block es;
	struct ext2_super_block e2s;
    } sb;
    union {
	struct xiafs_super_block xiasb;
	char romfs_magic[8];
    } xsb;
    union {
	struct iso_volume_descriptor iso;
	struct hs_volume_descriptor hs;
    } isosb;
    struct stat statbuf;

    /* opening and reading an arbitrary unknown path can have
       undesired side effects - first check that `device' refers
       to a block device */
    if (stat (device, &statbuf) || !S_ISBLK(statbuf.st_mode))
      return 0;

    fd = open(device, O_RDONLY);
    if (fd < 0)
      return 0;

    if (lseek(fd, BLOCK_SIZE, SEEK_SET) != BLOCK_SIZE
	|| read(fd, (char *) &sb, sizeof(sb)) != sizeof(sb))
	 goto io_error;

    if (sb.e2s.s_magic == EXT2_SUPER_MAGIC
	|| sb.e2s.s_magic == EXT2_PRE_02B_MAGIC
	|| sb.e2s.s_magic == swapped(EXT2_SUPER_MAGIC))
	 type = "ext2";

    else if (sb.ms.s_magic == MINIX_SUPER_MAGIC
	     || sb.ms.s_magic == MINIX_SUPER_MAGIC2)
	 type = "minix";

    else if (sb.es.s_magic == EXT_SUPER_MAGIC)
	 type = "ext";

    if (!type) {
	 if (lseek(fd, 0, SEEK_SET) != 0
	     || read(fd, (char *) &xsb, sizeof(xsb)) != sizeof(xsb))
	      goto io_error;

	 if (xsb.xiasb.s_magic == _XIAFS_SUPER_MAGIC)
	      type = "xiafs";
	 else if(!strncmp(xsb.romfs_magic, "-rom1fs-", 8))
	      type = "romfs";
    }

    if (!type) {
	 if (lseek(fd, 0x8000, SEEK_SET) != 0x8000
	     || read(fd, (char *) &isosb, sizeof(isosb)) != sizeof(isosb))
	      goto io_error;

	 if(strncmp(isosb.iso.id, ISO_STANDARD_ID, sizeof(isosb.iso.id)) == 0
	    || strncmp(isosb.hs.id, HS_STANDARD_ID, sizeof(isosb.hs.id)) == 0)
	      type = "iso9660";
    }

    close (fd);
    return(type);

io_error:
    perror(device);
    close(fd);
    return 0;
}

FILE *procfs;

static void
procclose(void) {
    if (procfs)
        fclose (procfs);
    procfs = 0;
}

static int
procopen(void) {
    return ((procfs = fopen(PROC_FILESYSTEMS, "r")) != NULL);
}

static char *
procnext(void) {
   char line[100];
   static char fsname[50];

   while (fgets(line, sizeof(line), procfs)) {
      if (sscanf (line, "nodev %[^\n]\n", fsname) == 1) continue;
      if (sscanf (line, " %[^ \n]\n", fsname) != 1) continue;
      return fsname;
   }
   return 0;
}

static int
is_in_proc(char *type) {
    char *fsname;

    if (procopen()) {
	while ((fsname = procnext()) != NULL)
	  if (!strcmp(fsname, type))
	    return 1;
    }
    return 0;
}

static int
already (char *spec, char *node) {
    struct mntent *me;
    int ret = 1;

    if ((me = getmntfile(node)) != NULL)
        error ("mount: according to mtab, %s is already mounted on %s",
	       me->mnt_fsname, node);
    else if ((me = getmntfile(spec)) != NULL)
        error ("mount: according to mtab, %s is mounted on %s",
	       spec, me->mnt_dir);
    else
        ret = 0;
    return ret;
}

/* Mount a single file system.  Return status,
   so don't exit on non-fatal errors.  */
 
static int
try_mount5 (char *spec, char *node, char **type, int flags, char *mount_opts) {
   char *fsname;
   
   if (*type && strcasecmp (*type, "auto") == 0)
      *type = NULL;

   if (!*type && !(flags & MS_REMOUNT)) {
      *type = fstype(spec);
      if (verbose) {
	  printf ("mount: you didn't specify a filesystem type for %s\n",
		  spec);
	  if (*type)
	    printf ("       I will try type %s\n", *type);
	  else
	    printf ("       I will try all types mentioned in %s\n",
		    PROC_FILESYSTEMS);
      }
   }

   if (*type || (flags & MS_REMOUNT))
      return mount5 (spec, node, *type, flags & ~MS_NOSYS, mount_opts);

   if (!procopen())
     return -1;
   while ((fsname = procnext()) != NULL) {
      if (tested (fsname))
	 continue;
      if (mount5 (spec, node, fsname, flags & ~MS_NOSYS, mount_opts) == 0) {
	 *type = xstrdup(fsname);
	 procclose();
	 return 0;
      } else if (errno != EINVAL) {
         *type = "guess";
	 procclose();
	 return 1;
      }
   }
   procclose();
   *type = NULL;

   return -1;
}

#if 0 /* WATCHDOG */
static
#endif /* WATCHDOG */
int
mount_one (char *spec0, char *node0, char *type0, char *opts0,
	   int freq, int pass)
{
  struct mntent mnt;
  int mnt_err;
  int flags;
  char *extra_opts;		/* written in mtab */
  char *mount_opts;		/* actually used on system call */
  static int added_ro = 0;
  int loop, looptype, offset;
  char *spec, *node, *type, *opts, *loopdev, *loopfile;

  spec = xstrdup(spec0);
  node = xstrdup(node0);
  type = xstrdup(type0);
  opts = xstrdup(opts0);

/*
 * There are two special cases: nfs mounts and loop mounts.
 * In the case of nfs mounts spec is probably of the form machine:path.
 * In the case of a loop mount, either type is of the form lo@/dev/loop5
 * or the option "-o loop=/dev/loop5" or just "-o loop" is given, or
 * mount just has to figure things out for itself from the fact that
 * spec is not a block device. We do not test for a block device
 * immediately: maybe later other types of mountable objects will occur.
 */

  if (type == NULL) {
      if (strchr (spec, ':') != NULL) {
	type = "nfs";
	if (verbose)
	  printf("mount: no type was given - "
		 "I'll assume nfs because of the colon\n");
      }
  }

  parse_opts (xstrdup (opts), &flags, &extra_opts);

  /* root may allow certain types of mounts by ordinary users */
  if (suid && !(flags & MS_USER)) {
      if (already (spec, node))
	die (3, "mount failed");
      else
        die (3, "mount: only root can mount %s on %s", spec, node);
  }

  /* quietly succeed for fstab entries that don't get mounted automatically */
  if (all && (flags & MS_NOAUTO))
    return 0;

  mount_opts = extra_opts;

  /*
   * A loop mount?
   * Only test for explicitly specified loop here,
   * and try implicit loop if the mount fails.
   */
  loopdev = opt_loopdev;

  looptype = (type && strncmp("lo@", type, 3) == 0);
  if (looptype) {
    if (loopdev)
      error("mount: loop device specified twice");
    loopdev = type+3;
    type = opt_vfstype;
  }
  else if (opt_vfstype) {
    if (type)
      error("mount: type specified twice\n");
    else
      type = opt_vfstype;
  }

  loop = ((flags & MS_LOOP) || loopdev || opt_offset || opt_encryption);
  loopfile = spec;

  if (loop) {
    flags |= MS_LOOP;
    if (fake) {
      if (verbose)
	printf("mount: skipping the setup of a loop device\n");
    } else {
      int loopro = (flags & MS_RDONLY);

      if (!loopdev || !*loopdev)
	loopdev = find_unused_loop_device();
      if (!loopdev)
	return 1;
      if (verbose)
	printf("mount: going to use the loop device %s\n", loopdev);
      offset = opt_offset ? strtoul(opt_offset, NULL, 0) : 0;
      if (set_loop (loopdev, loopfile, offset, opt_encryption, &loopro))
	return 1;
      spec = loopdev;
      if (loopro)
	flags |= MS_RDONLY;
    }
  }

  if (!fake && type && streq (type, "nfs"))
#ifdef HAVE_NFS
    if (nfsmount (spec, node, &flags, &extra_opts, &mount_opts) != 0)
      return 1;
#else
    die (1, "mount: this version was compiled without support for the type `nfs'");
#endif

  block_signals (SIG_BLOCK);

  if (fake
      || (try_mount5 (spec, node, &type, flags & ~MS_NOSYS, mount_opts)) == 0)
    /* Mount succeeded, report this (if verbose) and write mtab entry.  */
    {
      if (loop)
	  opt_loopdev = loopdev;

      mnt.mnt_fsname = canonicalize (loop ? loopfile : spec);
      mnt.mnt_dir = canonicalize (node);
      mnt.mnt_type = type ? type : "unknown";
      mnt.mnt_opts = fix_opts_string (flags & ~MS_NOMTAB, extra_opts);
      mnt.mnt_freq = freq;
      mnt.mnt_passno = pass;
      
      /* We get chatty now rather than after the update to mtab since the
	 mount succeeded, even if the write to /etc/mtab should fail.  */
      if (verbose)
	   print_one (&mnt);

      if (!nomtab) {
	  if (flags & MS_REMOUNT) {
	    close_mtab ();
	    update_mtab (mnt.mnt_dir, &mnt);
	    open_mtab ("a+");
	  } else {
	    if ((addmntent (F_mtab, &mnt)) == 1)
	      die (1, "mount: error writing %s: %s",
		   MOUNTED, strerror (errno));
	  }
      }

      block_signals (SIG_UNBLOCK);
      return 0;
    }

  mnt_err = errno;

  if (loop)
	del_loop(spec);

  block_signals (SIG_UNBLOCK);

  /* Mount failed, complain, but don't die.  */

  if (type == 0)
    error ("mount: you must specify the filesystem type");
  else
  switch (mnt_err)
    {
    case EPERM:
      if (geteuid() == 0) {
	   struct stat statbuf;
	   if (stat (node, &statbuf) || !S_ISDIR(statbuf.st_mode))
		error ("mount: mount point %s is not a directory", node);
	   else
		error ("mount: permission denied");
      } else
	error ("mount: must be superuser to use mount");
      break;
    case EBUSY:
      if (flags & MS_REMOUNT) {
	error ("mount: %s is busy", node);
      } else {
	error ("mount: %s already mounted or %s busy", spec, node);
	already (spec, node);
      }
      break;
    case ENOENT:
      { struct stat statbuf;
	if (lstat (node, &statbuf))
	      error ("mount: mount point %s does not exist", node);
	else if (stat (node, &statbuf))
	      error ("mount: mount point %s is a symbolic link to nowhere",
		     node);
	else if (stat (spec, &statbuf))
	      error ("mount: special device %s does not exist", spec);
	else {
           errno = mnt_err;
           perror("mount");
	}
	break;
      }
    case ENOTDIR:
      error ("mount: mount point %s is not a directory", node);
      break;
    case EINVAL:
    { int fd, size;
      struct stat statbuf;

      if (flags & MS_REMOUNT) {
	error ("mount: %s not mounted already, or bad option\n", node);
      } else {
	error ("mount: wrong fs type, bad option, bad superblock on %s,\n"
	       "       or too many mounted file systems",
	       spec);

	if (stat (spec, &statbuf) == 0 && S_ISBLK(statbuf.st_mode)
	   && (fd = open(spec, O_RDONLY)) >= 0) {
	  if(ioctl(fd, BLKGETSIZE, &size) == 0 && size <= 2)
	  error ("       (aren't you trying to mount an extended partition,\n"
		 "       instead of some logical partition inside?)\n");
	  close(fd);
	}
      }
      break;
    }
    case EMFILE:
      error ("mount table full"); break;
    case EIO:
      error ("mount: %s: can't read superblock", spec); break;
    case ENODEV:
      if (is_in_proc(type) || !strcmp(type, "guess"))
        error("mount: %s has wrong major or minor number", spec);
      else if (procfs) {
	char *lowtype, *p;
	int u;

	error ("mount: fs type %s not supported by kernel", type);

	/* maybe this loser asked for FAT or ISO9660 or isofs */
	lowtype = xstrdup(type);
	u = 0;
	for(p=lowtype; *p; p++) {
	  if(tolower(*p) != *p) {
	    *p = tolower(*p);
	    u++;
	  }
	}
	if (u && is_in_proc(lowtype))
	  error ("mount: probably you meant %s", lowtype);
	else if (!strncmp(lowtype, "iso", 3) && is_in_proc("iso9660"))
	  error ("mount: maybe you meant iso9660 ?");
	free(lowtype);
      } else
	error ("mount: %s has wrong device number or fs type %s not supported",
	       spec, type);
      break;
    case ENOTBLK:
      { struct stat statbuf;

	if (stat (spec, &statbuf)) /* strange ... */
	  error ("mount: %s is not a block device, and stat fails?", spec);
	else if (S_ISBLK(statbuf.st_mode))
	  error ("mount: the kernel does not recognize %s as a block device\n"
		 "       (maybe `insmod driver'?)", spec);
	else if (S_ISREG(statbuf.st_mode))
	  error ("mount: %s is not a block device (maybe try `-o loop'?)",
		 spec);
	else
	  error ("mount: %s is not a block device", spec);
      }
      break;
    case ENXIO:
      error ("mount: %s is not a valid block device", spec); break;
    case EACCES:  /* pre-linux 1.1.38, 1.1.41 and later */
    case EROFS:   /* linux 1.1.38 and later */
      if (added_ro) {
          error ("mount: block device %s is not permitted on its filesystem",
		 spec);
          break;
      } else {
         added_ro = 1;
	 if (loop) {
	     opts = opts0;
	     type = type0;
	 }
         if (opts) {
             opts = realloc(xstrdup(opts), strlen(opts)+4);
             strcat(opts, ",ro");
         } else
             opts = "ro";
	 if (type && !strcmp(type, "guess"))
	     type = 0;
         error ("mount: %s%s is write-protected, mounting read-only",
		loop ? "" : "block device ", spec0);
	 return mount_one (spec0, node0, type, opts, freq, pass);
      }
      break;
    default:
      error ("mount: %s", strerror (mnt_err)); break;
    }
  return 1;
}

/* Check if an fsname/dir pair was already in the old mtab.  */
static int
mounted (char *spec, char *node, string_list spec_list, string_list node_list)
{
  spec = canonicalize (spec);
  node = canonicalize (node);

  while (spec_list != NULL)
    {
      if (streq (spec, car (spec_list)) && streq (node, car (node_list)))
	return 1;
      spec_list = cdr (spec_list);
      node_list = cdr (node_list);
    }
    return 0;
}

#if 0 /* WATCHDOG */
/* Mount all filesystems of the specified types except swap and root.  */
static int
mount_all (string_list types)
{
  struct mntent *fstab;
  struct mntent *mnt;
  string_list spec_list = NULL;
  string_list node_list = NULL;
  int status;

  rewind (F_mtab);

  while ((mnt = getmntent (F_mtab)))
    if (matching_type (mnt->mnt_type, types)
	&& !streq (mnt->mnt_dir, "/")
	&& !streq (mnt->mnt_dir, "root"))
      {
	spec_list = cons (xstrdup (mnt->mnt_fsname), spec_list);
	node_list = cons (xstrdup (mnt->mnt_dir), node_list);
      }

  status = 0;
  if (!setfsent())
    return 1;
  while ((fstab = getfsent ()) != NULL)
    if (matching_type (fstab->mnt_type, types)
	 && !streq (fstab->mnt_dir, "/")
	 && !streq (fstab->mnt_dir, "root"))
      if (mounted (fstab->mnt_fsname, fstab->mnt_dir, spec_list, node_list))
	{
	  if (verbose)
	    printf("mount: %s already mounted on %s\n",
		   fstab->mnt_fsname, fstab->mnt_dir);
	}
      else
        status |= mount_one (fstab->mnt_fsname, fstab->mnt_dir,
			     fstab->mnt_type, fstab->mnt_opts,
			     fstab->mnt_freq, fstab->mnt_passno);

  return status;
}


/* Create mtab with a root entry.  */
static void
create_mtab (void)
{
  struct mntent *fstab;
  struct mntent mnt;
  int flags;
  char *extra_opts;

  if ((F_mtab = setmntent (MOUNTED, "a+")) == NULL)
    die (1, "mount: can't open %s for writing: %s", MOUNTED, strerror (errno));

  /* Find the root entry by looking it up in fstab, which might be wrong.
     We could statfs "/" followed by a slew of stats on /dev/ but then
     we'd have to unparse the mount options as well....  */
  if ((fstab = getfsfile ("/")) || (fstab = getfsfile ("root"))) {
      parse_opts (xstrdup (fstab->mnt_opts), &flags, &extra_opts);
      mnt = *fstab;
      mnt.mnt_fsname = canonicalize (fstab->mnt_fsname);
      mnt.mnt_dir = "/";
      mnt.mnt_opts = fix_opts_string (flags, extra_opts);

      if (addmntent (F_mtab, &mnt) == 1)
	die (1, "mount: error writing %s: %s", MOUNTED, strerror (errno));
  }
  close_mtab();
}


extern char version[];
static struct option longopts[] =
{
  { "all", 0, 0, 'a' },
  { "fake", 0, 0, 'f' },
  { "help", 0, 0, 'h' },
  { "no-mtab", 0, 0, 'n' },
  { "read-only", 0, 0, 'r' },
  { "ro", 0, 0, 'r' },
  { "verbose", 0, 0, 'v' },
  { "version", 0, 0, 'V' },
  { "read-write", 0, 0, 'w' },
  { "rw", 0, 0, 'w' },
  { "options", 1, 0, 'o' },
  { "types", 1, 0, 't' },
  { NULL, 0, 0, 0 }
};

static const char *usage_string = "\
usage: mount [-hV]\n\
       mount -a [-nfrvw] [-t vfstypes]\n\
       mount [-nfrvw] [-o options] special | node\n\
       mount [-nfrvw] [-t vfstype] [-o options] special node\n\
";

static void
usage (FILE *fp, int n)
{
  fprintf (fp, "%s", usage_string);
  unlock_mtab();
  exit (n);
}

int
main (int argc, char *argv[]) {
  int c, err, lth;
  char *options = NULL, *otmp;
  string_list types = NULL;
  struct mntent *fs;
  char *spec;
  int result = 0;
  struct stat statbuf;

  while ((c = getopt_long (argc, argv, "afhnrvVwt:o:", longopts, NULL)) != EOF)
    switch (c) {
      case 'a':			/* mount everything in fstab */
	++all;
	break;
      case 'f':			/* fake (don't actually do mount(2) call) */
	++fake;
	break;
      case 'h':			/* help */
	usage (stdout, 0);
	break;
      case 'n':			/* mount without writing in /etc/mtab */
	++nomtab;
	break;
      case 'o':			/* specify mount options */
	lth = strlen(optarg);	/* allow repeated -o options */
	if (options)
	     lth += 1 + strlen(options);
	otmp = xmalloc(lth+1);
	*otmp = 0;
	if (options) {
	     strcat(otmp, options);
	     strcat(otmp, ",");
	     free(options);
	}
	strcat(otmp, optarg);
	options = otmp;
	break;
      case 'r':			/* mount readonly */
	++readonly;
	readwrite = 0;
	break;
      case 't':			/* specify file system types */
	types = parse_list (optarg);
	break;
      case 'v':			/* be chatty - very chatty if repeated */
	++verbose;
	break;
      case 'V':			/* version */
	printf ("mount: %s\n", version);
	exit (0);
      case 'w':			/* mount read/write */
	++readwrite;
	readonly = 0;
	break;
      case 0:
	break;
      case '?':
      default:
	usage (stderr, 1);
	break;
    }

  argc -= optind;
  argv += optind;

  if (argc == 0) {
      if (options)
	usage (stderr, 1);
      if (!all)
	return print_all (types);
  }

  if (getuid () != geteuid ()) {
      suid = 1;
      if (types || options || readwrite || nomtab || all || fake || argc != 1)
	die (2, "mount: only root can do that");
  }

  err = stat(MOUNTED, &statbuf);
  if (!nomtab) {
      lock_mtab ();
      if (err)
	  create_mtab ();
      open_mtab ("a+");
  } else {
      if (!err)
	  open_mtab ("r");
  }

  switch (argc)
    {
    case 0:
      /* mount -a */
      result = mount_all (types);
      break;

    case 1:
      /* mount [-nfrvw] [-o options] special | node */
      if (types != NULL)
	usage (stderr, 1);
      /* Try to find the other pathname in fstab.  */ 
      spec = canonicalize (*argv);
      if (!(fs = getmntfile (spec))
	  && !(fs = getfsspec (spec)) && !(fs = getfsfile (spec)))
	die (2, "mount: can't find %s in %s or %s",
	     spec, MOUNTED, _PATH_FSTAB);
      /* Merge the fstab and command line options.  */
      if (options == NULL)
	options = fs->mnt_opts;
      else
	{
	  char *tmp = xmalloc(strlen(fs->mnt_opts) + strlen(options) + 2);

	  sprintf (tmp, "%s,%s", fs->mnt_opts, options);
	  options = tmp;
	}
      result = mount_one (xstrdup (fs->mnt_fsname), xstrdup (fs->mnt_dir),
			  xstrdup (fs->mnt_type), options,
			  fs->mnt_freq, fs->mnt_passno);
      break;

    case 2:
      /* mount [-nfrvw] [-t vfstype] [-o options] special node */
      if (types == NULL)
	result = mount_one (argv[0], argv[1], NULL, options, 0, 0);
      else if (cdr (types) == NULL)
	result = mount_one (argv[0], argv[1], car (types), options, 0, 0);
      else
	usage (stderr, 2);
      break;
      
    default:
      usage (stderr, 2);
    }

  if (!nomtab)
    {
      endmntent (F_mtab);
      unlock_mtab ();
    }

  exit (result);
}
#endif /* WATCHDOG */
