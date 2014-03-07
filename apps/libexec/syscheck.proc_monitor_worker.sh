#!/bin/bash

cmdline_rhel='/opt/monitor/bin/monitor --worker /opt/monitor/var/rw/nagios.qh'
cmdline_sles="$cmdline_rhel"
lockfile_rhel=''
lockfile_sles=''
max='100'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
