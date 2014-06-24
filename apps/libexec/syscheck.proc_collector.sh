#!/bin/bash

cmdline_rhel='/opt/trapper/bin/collector -On -c /opt/trapper/etc/collector.conf -A -Lsd -p /opt/trapper/var/run/collector.run'
cmdline_sles="$cmdline_rhel"
lockfile_rhel='/opt/trapper/var/run/collector.run'
lockfile_sles="$lockfile_rhel"
max='1'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
