#!/bin/bash

cmdline_rhel='/usr/bin/lua /opt/synergy/bin/synergy --monitor'
cmdline_sles='/usr/bin/lua5.1 /opt/synergy/bin/synergy --monitor'
lockfile_rhel='/opt/synergy/var/run/processor.run'
lockfile_sles="$lockfile_rhel"
max='1'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
