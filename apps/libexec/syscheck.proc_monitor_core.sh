#!/bin/bash

cmdline_rhel='/opt/monitor/bin/monitor -d /opt/monitor/etc/nagios.cfg'
cmdline_sles="$cmdline_rhel"
lockfile_rhel='/opt/monitor/var/nagios.lock'
lockfile_sles="$lockfile_rhel"
max='2'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
