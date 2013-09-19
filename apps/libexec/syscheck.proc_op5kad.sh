#!/bin/bash

cmdline='/usr/bin/python /usr/bin/op5kad -c /etc/op5kad/kad.conf'
lockfile='/var/run/op5kad.pid'


# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
