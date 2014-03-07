#!/bin/bash

cmdline_rhel='/usr/libexec/mysqld --basedir=/usr --datadir=/var/lib/mysql --user=mysql --log-error=/var/log/mysqld.log --pid-file=/var/run/mysqld/mysqld.pid --socket=/var/lib/mysql/mysql.sock'
cmdline_sles='/usr/sbin/mysqld --basedir=/usr --datadir=/var/lib/mysql --user=mysql --pid-file=/var/lib/mysql/mysqld.pid --skip-external-locking --port=3306 --socket=/var/lib/mysql/mysql.sock'
lockfile_rhel='/var/run/mysqld/mysqld.pid'
lockfile_sles='/var/lib/mysql/mysqld.pid'
max='1'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
