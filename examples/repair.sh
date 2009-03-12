#!/bin/sh

#
# I try to get a repair script that can handle as many problems as possible.
# Feel free to send me some additions.
#
# (C) Michael Meskes <meskes@topsystem.de> Mon Jun 23 13:40:15 CEST 1997
# Placed under GPL.
#

#
# whom to send mail
#
admin=root

#
# let's see what error message we got
#
case $1 in
#
#	ENFILE: file table overflow
#	=> increase file-max by 10%
#
 23) 	
	fm=`cat /proc/sys/kernel/file-max`
	fm=`expr $fm + $fm / 10`
	echo $fm > /proc/sys/kernel/file-max
#
#	create log entry
#
	echo "increased file-max to "$fm | logger -i -t repair -p daemon.info 
#
#	that's it, problem disappeared
#
	exit 0;;
#
#	ENETDOWN: network is down
#	ENETUNREACH: network is unreachable
#	=> try to reconfigure network interface, there is no guarantee that
#	   this helps, but if it does not, reboot won't either
#	   Note: This is for Debian! Please adjust to your distribution and
#	         tell me what you had to change!
#		 Also, this is for machine with only one network card.
#
	
100|101)
	ifconfig eth0 down
	ifconfig lo down
	ethmodule=`grep eth0 /etc/conf.modules | grep "^alias" | cut -f 3 -d' '`
	if [ -n $ethmodule ]
	then
#
#		sometime it helps to remove the module, in
#		particular for drivers under development
#
		modprobe -r $ethmodule
#
#		put it back in since we're not sure if kerneld
#		handles this
#
		modprobe $ethmodule
	fi
#
#	bring it back up
#
	/etc/init.d/network
#
#	create log entry
#
		echo "re-initialized network interface eth0" | logger -i -t repair -p daemon.info 
#		
#	that' all we can do here
#
	exit 0;;
esac

#
# couldn't do anything
# tell the sysadmin what's going on
#
if [ -x /usr/bin/mail ]
then
	echo `hostname`" is going down because of error "$1|/usr/bin/mail -s "System fault!" ${admin}
fi
#
# finally tell watchdog to reboot
#
exit $1
