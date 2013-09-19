#!/bin/bash

cmdline='/opt/monitor/bin/monitor -d /opt/monitor/etc/nagios.cfg'
lockfile='/opt/monitor/var/nagios.lock'
max='2'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
