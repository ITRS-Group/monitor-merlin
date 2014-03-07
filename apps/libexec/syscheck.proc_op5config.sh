#!/bin/bash

cmdline_rhel='/usr/bin/php -q /opt/op5sys/bin/config-daemon.php -p /var/run/op5config.pid -d'
cmdline_sles="$cmdline_rhel"
lockfile_rhel='/var/run/op5config.pid'
lockfile_sles="$lockfile_sles"
max='1'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
