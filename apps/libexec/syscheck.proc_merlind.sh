#!/bin/bash

cmdline='/usr/bin/merlind -c /opt/monitor/op5/merlin/merlin.conf'
lockfile='/var/run/merlin.pid'


# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
