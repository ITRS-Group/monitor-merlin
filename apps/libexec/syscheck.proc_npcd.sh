#!/bin/bash

cmdline_rhel='/opt/monitor/bin/npcd -d -f /opt/monitor/etc/pnp/npcd.cfg'
cmdline_sles="$cmdline_rhel"
lockfile_rhel='/var/run/npcd.pid'
lockfile_sles="$lockfile_rhel"
max='1'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
