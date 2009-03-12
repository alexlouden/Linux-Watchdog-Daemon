# This is the Makefile of watchdog
#
# defines are in include/config.h now
#
# Where do you want to install watchdog?
#
DESTDIR=
SBINDIR=$(DESTDIR)/usr/sbin
MANDIR=$(DESTDIR)/usr/man
ETCDIR=$(DESTDIR)/etc
#
CC=gcc
#
# You shouldn't have to change anything below here
#
#CFLAGS=-g -O2 -Wall ${SYSLOG} ${DEV} ${TEMP} ${PID} ${RS} ${TM} ${SL} ${MNT} ${MAX} ${MIN} ${MAXT} ${SEND} ${PS} ${AD} ${RT} ${PRI} ${CL} ${CF} -Iinclude
CFLAGS=-g -Wall ${SYSLOG} ${DEV} ${TEMP} ${PID} ${RS} ${TM} ${SL} ${MNT} ${MAX} ${MIN} ${MAXT} ${SEND} ${PS} ${AD} ${RT} ${PRI} ${CL} ${CF} -Iinclude -I/usr/src/linux-dev/include
#LDFLAGS=-s
LDFLAGS=
#
OBJECTS=watchdog.o checks/file_stat.o checks/file_table.o checks/keep_alive.o\
	checks/load.o checks/net.o checks/temp.o checks/test_binary.o\
	system/quotactl.o system/ifdown.o system/shutdown.o\
	mount/sundries.o mount/umount.o mount/mntent.o\
	mount/lomount.o mount/fstab.o mount/version.o mount/mount.o\
	mount/nfsmount.o mount/nfsmount_clnt.o mount/nfsmount_xdr.o\
	mount/nfsmount.o

all: watchdog

watchdog: $(OBJECTS)

watchdog.o: include/config.h include/extern.h include/watch_err.h\
		include/glibc_compat.h watchdog.c

checks/*.o: include/config.h include/extern.h include/watch_err.h\
                include/glibc_compat.h 

mount/nfsmount.o: include/nfs_mountversion.h include/nfs_mount3.h

include/nfs_mountversion.h:
	rm -f include/nfs_mountversion.h
	if [ -f /usr/include/linux/nfs_mount.h ]; then \
		grep NFS_MOUNT_VERSION /usr/include/linux/nfs_mount.h \
		| sed -e 's/NFS/KERNEL_NFS/'; \
	else \
		echo '#define KERNEL_NFS_MOUNT_VERSION 0'; \
	fi > include/nfs_mountversion.h

install: all
	install -d $(SBINDIR)
	install -g root -o root -m 700 -s watchdog $(SBINDIR)
	install -g root -o root -m 700 -s examples/repair.sh $(SBINDIR)/repair
	install -d $(MANDIR)/man5 $(MANDIR)/man8
	install -g root -o root -m 644 watchdog.8 $(MANDIR)/man8
	install -g root -o root -m 644 watchdog.conf.5 $(MANDIR)/man5
	install -d $(ETCDIR)
	install -g root -o root -m 644 watchdog.conf $(ETCDIR)

clean:
	/bin/rm -f watchdog *.o mount/*.o checks/*.o system/*.o\
		 *~ mount/*~ checks/*~ system/*~ 2>/dev/null
