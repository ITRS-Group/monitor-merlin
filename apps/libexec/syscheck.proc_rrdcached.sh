#!/bin/bash

cmdline='/usr/bin/rrdcached -p /opt/monitor/var/rrdtool/rrdcached/rrdcached.pid -m 0777 -l unix:/opt/monitor/var/rrdtool/rrdcached/rrdcached.sock -b /opt/monitor/var/rrdtool/rrdcached -P FLUSH,PENDING -z 1800 -w 1800 -s root -j /opt/monitor/var/rrdtool/rrdcached/spool -p /opt/monitor/var/rrdtool/rrdcached/rrdcached.pid'
lockfile='/opt/monitor/var/rrdtool/rrdcached/rrdcached.pid'


# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
