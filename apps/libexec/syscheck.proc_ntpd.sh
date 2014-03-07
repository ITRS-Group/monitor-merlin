#!/bin/bash

cmdline_rhel='ntpd -u ntp:ntp -p /var/run/ntpd.pid -g'
cmdline_sles='/usr/sbin/ntpd -p /var/run/ntp/ntpd.pid -g -u ntp:ntp -i /var/lib/ntp -c /etc/ntp.conf'
lockfile_rhel='/var/run/ntpd.pid'
lockfile_sles='/var/run/ntp/ntpd.pid'
max='1'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
