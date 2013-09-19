#!/bin/bash

cmdline='/opt/monitor/bin/npcd -d -f /opt/monitor/etc/pnp/npcd.cfg'
lockfile='/var/run/npcd.pid'


# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
