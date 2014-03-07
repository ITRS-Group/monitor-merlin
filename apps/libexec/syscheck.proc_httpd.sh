#!/bin/bash

cmdline_rhel='/usr/sbin/httpd'
cmdline_sles='/usr/sbin/httpd2-prefork -f /etc/apache2/httpd.conf -DSSL'
lockfile_rhel='/var/run/httpd/httpd.pid'
lockfile_sles='/var/run/httpd2.pid'
max='50'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
