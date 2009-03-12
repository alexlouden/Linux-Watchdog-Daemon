sbin_PROGRAMS = watchdog

watchdog_SOURCES = file_stat.c file_table.c fstab.c ifdown.c keep_alive.c \
			load.c lomount.c mntent.c mount.c net.c nfsmount.c \
			nfsmount_clnt.c nfsmount_xdr.c quotactl.c shutdown.c sundries.c \
			temp.c test_binary.c umount.c version.c watchdog.c

INCLUDES = -I@top_srcdir@/include

distclean-depend:
	rm -rf .deps
