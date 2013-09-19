#!/bin/bash

cmdline='/opt/monitor/bin/monitor --worker /opt/monitor/var/rw/nagios.qh'

max='100000'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
