/*
 * this file contains some information still missing from glibc header files
 */

/*
 * from mntent.h
 */

#define MNTTYPE_EXT2            "ext2"          /* Second Extended file system */

#define MNTOPT_NOQUOTA          "noquota"       /* don't use any quota on this partition */
#define MNTOPT_USRQUOTA         "usrquota"      /* use userquota on this partition */
#define MNTOPT_GRPQUOTA         "grpquota"      /* use groupquota on this partition */

/*
 * to get getsid()
 */
 
#define __USE_XOPEN_EXTENDED

/*
 * to get typedef for __u32, is not needed for newer glibc sources, at least on Debian
 */
 
#include <asm/types.h>
