#!/bin/bash

cmdline_rhel='crond'
cmdline_sles='/usr/sbin/cron'
lockfile_rhel='/var/run/crond.pid'
lockfile_sles="$lockfile_sles"
max='1'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
