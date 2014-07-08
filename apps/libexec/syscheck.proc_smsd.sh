#!/bin/bash

cmdline_rhel='/usr/sbin/smsd'
cmdline_sles="$cmdline_rhel"
lockfile_rhel='/var/run/smsd/smsd.pid'
lockfile_sles="$lockfile_rhel"
max='2'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
