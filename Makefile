# This is the Makefile of watchdog
#
# General watchdog defines.
#
# If you don't want to log any activity uncomment the following.
# I strongly discourage this, though.
#
SYSLOG = -DUSE_SYSLOG
#
# How long does watchdog sleep between two passes? 10s is the default.
#
#SI = -DSLEEP_INTERVAL=10  
#
# If you do not want to send any mail comment the following line.
#
SEND = -DSENDTOADMIN
#
# Address to mail to. Default is root
#
#AD = -DSYSADMIN=\"root\"
#
# Do you want watchdog to act like a real-time application
# (i.e. lock its pages in memory)?
#
RT = -DREALTIME
#
# Now some check specific defines.
#
# Maximal 1 min load average.
#
#MAX = -DMAXLOAD=12
#
# Minimal 1 min load average, the lowest value that is accepted as a maxload parameter.
#
#MIN = -DMINLOAD=2
#
# Maximal temperature (make sure you use the same unit as
# your watchdog hardware).
#
#MAXT = -DMAXTEMP=120
#
# how long are the lines in our config file
#
#CL = -DCONFIG_LINE_LEN=80

# The next parameters define the files to be accessed.
# You shouldn´t need to change any of these. The values listed below
# are the defaults.
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
#PID = -DPIDFILE=\"/var/run/watchdog.pid\"
#
# where do we save the random seed, set to \"\" to disable
#
#RS = -DRANDOM_SEED=\"/var/run/random-seed\"
#
# where is our config file
#
#CF = -DCONFIG_FILENAME=\"/etc/watchdog.conf"
CF = -DCONFIG_FILENAME=\"./watchdog.conf\"

# And some system specific defines. Should be okay on any system.
#
# Kernel timer margin.
#
#TM = -DTIMER_MARGIN=60  
#
# Where is your sendmail binary (default is _PATH_SENDMAIL).
#
#PS = -DPATH_SENDMAIL=\"/usr/sbin/sendmail\" 
#
# Which priority should watchdog use when scheduled as real-time application?
#
#PRI = -DSCHEDULE_PRIORITY=1

#
# For the code taken from mount.
#
MNT = -DHAVE_NFS -DFSTYPE_DEFAULT=\"iso9660\"

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
MAJOR_VERSION = 4
MINOR_VERSION = 0
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

watchdog.o: include/version.h include/extern.h include/watch_err.h\
		include/glibc_compat.h watchdog.c

checks/*.o: include/version.h include/extern.h include/watch_err.h\
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

include/version.h: Makefile
	@echo "/* actual version */" > include/version.h
	@echo "/* DO NOT EDIT! */" >> include/version.h
	@echo "#define MAJOR_VERSION ${MAJOR_VERSION}" >> include/version.h
	@echo "#define MINOR_VERSION ${MINOR_VERSION}" >> include/version.h

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
	/bin/rm -f watchdog include/version.h *.o mount/*.o checks/*.o system/*.o\
		 *~ mount/*~ checks/*~ system/*~
