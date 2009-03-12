# This is the Makefile of watchdog
#
# If you don't want to log any activity uncomment the following.
# I strongly discourgae this, though.
#
SYSLOG = -DUSE_SYSLOG
#
# What's the name of your watchdog device?
# Leave DEV empty to disable keep alive support per default.
#
DEV = -DDEVNAME=\"/dev/watchdog\"
#
# What's the name of your temperature device?
# Leave TEMP empty to disable temperature checking per default.
# 
TEMP = -DTEMPNAME=\"/dev/temperature\"
#
# name of the PID file
#
PID = -DPIDFILE=\"/var/run/watchdog.pid\"
#
# where do we save the random seed, set to \"\" to disable
#
RS = -DRANDOM_SEED=\"/var/run/random-seed\"
#
# kernel timer margin 
#
TM = -DTIMER_MARGIN=60  
#
# how long does watchdog sleep between two passes 
#
SI = -DSLEEP_INTERVAL=10  
#
# maximal 1 min load average
#
MAX = -DMAXLOAD=12
#
# minimal 1 min load average, the lowest value that is accepted
#
MIN = -DMINLOAD=2
#
# maximal temperature (make sure you use the same unit as
# your watchdog hardware)
#
MAXT = -DMAXTEMP=120
#
# if you do not want to send any mail comment the following line
#
SEND = -DSENDTOADMIN
#
# where is your sendmail binary (default is _PATH_SENDMAIL)
#
#PS = -DPATH_SENDMAIL=\"/usr/sbin/sendmail\" 
#
# address to mail to
#
AD = -DSYSADMIN=\"root\"
#
# Mount defaults:
#
DEFAULT_FSTYPE=\"iso9660\"
#
# you need rpcgen and libc-4.2 or rpclib to compile in the NFS support
# pregenerated files are included.
# Make sure nfsmount_clnt.c is newer than nfsmount.x to avoid gcc complaints.
#
MNT = -DHAVE_NFS -DFSTYPE_DEFAULT=$(DEFAULT_FSTYPE)
#
# Were do you want to install watchdog?
#
DESTDIR=
SBINDIR=$(DESTDIR)/usr/sbin
MANDIR=${DESTDIR}/usr/man/man8
#
CC=gcc
#
# You shouldn't have to change anything below here
#
#CFLAGS=-g -O2 -Wall ${SYSLOG} ${DEV} ${TEMP} ${PID} ${RS} ${TM} ${SL} ${MNT} ${MAX} ${MIN} ${MAXT} ${SEND} ${PS} ${AD} -Imount
CFLAGS=-g -Wall ${SYSLOG} ${DEV} ${TEMP} ${PID} ${RS} ${TM} ${SL} ${MNT} ${MAX} ${MIN} ${MAXT} ${SEND} ${PS} ${AD} -Imount
#LDFLAGS=-s
LDFLAGS=
#
MAJOR_VERSION = 3
MINOR_VERSION = 3
#
OBJECTS=watchdog.o quotactl.o ifdown.o mount/sundries.o mount/umount.o\
	mount/lomount.o mount/fstab.o mount/version.o mount/mount.o mount/nfsmount.o\
	mount/nfsmount_clnt.o mount/nfsmount_xdr.o

all: watchdog

watchdog: $(OBJECTS)

watchdog.o: version.h watchdog.c

mount/nfsmount.o: mount/nfs_mountversion.h mount/nfs_mount3.h

mount/nfs_mountversion.h:
	rm -f nfs_mountversion.h
	if [ -f /usr/include/linux/nfs_mount.h ]; then \
		grep NFS_MOUNT_VERSION /usr/include/linux/nfs_mount.h \
		| sed -e 's/NFS/KERNEL_NFS/'; \
	else \
		echo '#define KERNEL_NFS_MOUNT_VERSION 0'; \
	fi > nfs_mountversion.h

version.h: Makefile
	@echo "/* actual version */" > version.h
	@echo "/* DO NOT EDIT! */" >> version.h
	@echo "#define MAJOR_VERSION ${MAJOR_VERSION}" >> version.h
	@echo "#define MINOR_VERSION ${MINOR_VERSION}" >> version.h

install: all
	install -d $(SBINDIR)
	install -g root -o root -m 700 -s watchdog $(SBINDIR)
	install -g root -o root -m 700 -s examples/repair.sh $(SBINDIR)/repair
	install -d $(MANDIR)
	install -g root -o root -m 644 watchdog.8 $(MANDIR)

clean:
	/bin/rm watchdog version.h *.o mount/*.o *~ mount/*~
