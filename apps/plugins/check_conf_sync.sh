#!/bin/bash
# check_conf_sync.sh - Generates MD5 checksum of configuration files, matches
#                      it against that of the primary server (set in 'sync_utils.sh')
# Author: Andreas Ericsson <ae@op5.se>
#
#
# THIS SCRIPT SHOULD NEVER BE USED ALONGSIDE MERLIN!
# It's included for completeness so op5-affiliated consultants can migrate
# customers experiencing problems from a Merlin-based setup to the old way
# of doing distributed monitoring.
# DO NOT ENABLE THIS WHILE USING MERLIN FOR LOADBALANCED AND/OR DISTRIBUTED
# MONITORING! You have been (thoroughly) warned.
#
#
# Copyright (C) 2004 OP5 AB
# All rights reserved.
#
# Changelog
# 2004-02-03 - Creation date
# 2008-02-19 - Modified, added rsync as mirror method. Christian Nilsson <cn@op5.com>
# 2008-06-12 - Modified so that monitor only will be reloaded when recived new and
#              correct configuration.
#            - Added a little bit better check messages to Monitor.
#            - rsync is using a text file for the list of files to sync.
# 2008-08-15 - Added function rsync_status to take care of error msg from rsync
#            - Added posibility to send email when exit state is: 
#                WARNING
#                CRIICAL
#                UNKNOWN
#              Use this when using the script with crond.
#              Just set the variable error_to to a mail address to recieve erorr mail.
#              If you set the error_to you will not get any erorrs to stdout.
#            - Created a function used to go back to backuped config.
###############################################################################

ip_primary="10.10.10.100"
error_to="" # Leave empty if you do not want any error mail

dtime=`date`
commit_time=`date +%s`
restart_time=$((commit_time+10))
cmd_time=$((commit_time+11))
cmd_pipe=/opt/monitor/var/rw/nagios.cmd
echocmd="/bin/echo"

function state_ok () {
	if [ "$error_to" = "" ] ; then
		echo "$1"
	fi
	exit 0
}

function state_warning () {
	if [ "$error_to" != "" ] ; then
		error_msg="WARNING: Minor error when checking for updated config on master server: $ip_primary\n\nMessage:\n$1"
		echo -en $error_msg | mail -s "WARNING: check_conf_sync.sh on `hostname`" $error_to
	else
		echo "WARNING: $1"
	fi
	exit 1
}

function state_critical () {
	if [ "$error_to" != "" ] ; then
		error_msg="CRITICAL: Error when checking for updated config on master server: $ip_primary\n\nMessage:\n$1"
		echo -en $error_msg | mail -s "CRITICAL: check_conf_sync.sh on `hostname`" $error_to
	else
		echo "CRITICAL: $1"
	fi
	exit 2
}

function state_unknown () {
	if [ "$error_to" != "" ] ; then
		error_msg="UNKNOWN: Error when checking for updated config on master server: $ip_primary\n\nMessage:\n$1"
		echo -en $error_msg | mail -s "UNKNOWN: check_conf_sync.sh on `hostname`" $error_to
	else
		echo "UNKNOWN: $1"
	fi
	exit 3
}

function rsync_status () {
	rsync_status=$1

	if [ "$rsync_status" != "0" ] ; then
		cat /opt/monitor/var/conf_sync.log 
		revert_to_old_config
		state_critical "Error in rsync process during config sync (/opt/monitor/var/conf_sync.log for details): rsync exit code: $rsync_status"
	fi
}

function revert_to_old_config () {
	rm -rf /opt/monitor/etc
	cp -a $backupdir/etc /opt/monitor
   	chmod 775 /opt/monitor/etc
}

function update_sync_time_stamp () {
   tdate=`date +"%Y-%m-%d %H:%m:%S"`
   echo $tdate > /opt/monitor/var/conf_sync_time.stamp
}

backupdir=`mktemp -d /tmp/monitor_config_sync.XXXXXXX`
trap "rm -rf $backupdir" EXIT INT

umask 002
cd /opt/monitor

local_md5=`cat /opt/monitor/etc/_mon01col1uk/monitor_config.cfg \
			   /opt/monitor/etc/_admin_contacts.cfg \
			   /opt/monitor/etc/cgi.cfg \
			   /opt/monitor/etc/checkcommands.cfg \
			   /opt/monitor/etc/htpasswd.users \
			   /opt/monitor/etc/timeperiods.cfg \
			   /opt/monitor/etc/hostgroups.cfg.from_master \
			   /opt/monitor/etc/servicegroups.cfg.from_master \
			   /opt/monitor/op5/nagiosgraph/map_custom/wsp.map \
			   /opt/monitor/etc/misccommands.cfg |\
	md5sum | gawk '{ print $1 }'`

/opt/plugins/check_nrpe -H $ip_primary -c conf_syntax > /dev/null || state_critical "Master $ip_primary has error in the config. No config sync performed."
remote_md5=`/opt/plugins/check_nrpe -H $ip_primary -c conf_sync_mon01col1uk`

cp -a /opt/monitor/etc $backupdir

if ! [ "$local_md5" = "$remote_md5" ]; then
	# The first line is to use if you like to specify what file to sync
	rsync -avr --files-from=/opt/plugins/control/rsync-files.txt -e "ssh -i /etc/rsync/mirror-rsync-key" monitor@$ip_primary:/opt/monitor/etc /opt/monitor/etc > /opt/monitor/var/conf_sync.log 2>&1
	rsync_status $?
	rsync -avr -e "ssh -i /etc/rsync/mirror-rsync-key" monitor@$ip_primary:/opt/plugins/custom/ /opt/plugins/custom >> /opt/monitor/var/conf_sync.log 2>&1
	rsync_status $?
	rsync -avr -e "ssh -i /etc/rsync/mirror-rsync-key" monitor@$ip_primary:/opt/monitor/op5/nagiosgraph/map_custom/ /opt/monitor/op5/nagiosgraph/map_custom >> /opt/monitor/var/conf_sync.log 2>&1
	rsync_status $?

	cp /opt/monitor/etc/hostgroups.cfg /opt/monitor/etc/hostgroups.cfg.from_master
	cp /opt/monitor/etc/servicegroups.cfg /opt/monitor/etc/servicegroups.cfg.from_master

	# Removing hostgroups not in use on this poller:
	sed -n 's/hostgroups[\t ]*//p' /opt/monitor/etc/_mon01col1uk/monitor_config.cfg | sed 's/,/\n/g' | sed 's/ //g' |sort -u > /tmp/used_hostgroups
	for i in `grep hostgroup_name /opt/monitor/etc/hostgroups.cfg.from_master | awk '{print $2}'`; do
	inUse=`egrep -c "^\$i$" /tmp/used_hostgroups`
#		echo "inUse: $inUse"
		if [ "$inUse" -lt 1 ] ; then
#			echo "HostGroup: Not in use: $i"
			sed -i "/^# hostgroup '$i'/,/}/d" /opt/monitor/etc/hostgroups.cfg
		fi
	done

	# Removing servicegroups not in use on this poller:
	sed -n 's/servicegroups[\t ]*//p' /opt/monitor/etc/_mon01col1uk/monitor_config.cfg | sed 's/,/\n/g' | sed 's/ //g' |sort -u > /tmp/used_servicegroups
	for i in `grep servicegroup_name /opt/monitor/etc/servicegroups.cfg.from_master | awk '{print $2}'`; do
		inUse=`egrep -c "^\$i$" /tmp/used_servicegroups`
#		echo "inUse: $inUse"
		if [ "$inUse" -lt 1 ] ; then
#			echo "ServiceGroup: Not in use: $i"
			sed -i "/^# servicegroup '$i'/,/}/d" /opt/monitor/etc/servicegroups.cfg
		fi
	done

	/opt/monitor/bin/nagios -v /opt/monitor/etc/nagios.cfg > /dev/null
	ret=$?
	if [ $ret -ne 0 ]; then
		/opt/monitor/bin/nagios -v /opt/monitor/etc/nagios.cfg >> /opt/monitor/var/conf_sync.log 2>&1
		revert_to_old_config
		status_msg="Syntax check on new configuration failed. Rolling back the old one instead, without reloading Monitor."
		echo $status_msg  >> /opt/monitor/var/conf_sync.log 2>&1
		state_critical $status_msg
	else
		echo "[$commit_time] RESTART_PROGRAM;$restart_time" > $cmd_pipe
		echo "Configuration successfully synchronized. Monitor reoloaded." >> /opt/monitor/var/conf_sync.log 2>&1
		update_sync_time_stamp
		state_ok "Configuration successfully synchronized. Monitor reoloaded."
	fi

else
	echo "No synchronization needed." > /opt/monitor/var/conf_sync.log 2>&1
	timestamp=`cat /opt/monitor/var/conf_sync_time.stamp`
	#state_ok "No synchronization needed. Last sync: $timestamp"
	state_ok "Last successful sync: $timestamp"
fi

exit 0
