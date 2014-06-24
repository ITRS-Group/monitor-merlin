#!/bin/bash

cmdline_rhel='/usr/bin/processor -c /etc/processor/processor.conf'
cmdline_sles="$cmdline_rhel"
lockfile_rhel='/opt/trapper/var/run/processor.run'
lockfile_sles="$lockfile_rhel"
max='1'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
