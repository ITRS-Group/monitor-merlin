#!/bin/bash

cmdline_rhel='/sbin/syslog-ng -p /var/run/syslog-ng.pid'
cmdline_sles="$cmdline_rhel"
lockfile_rhel='/var/run/syslog-ng.pid'
lockfile_sles="$lockfile_rhel"
max='1'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
