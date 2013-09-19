#!/bin/bash

cmdline='/usr/libexec/mysqld --basedir=/usr --datadir=/var/lib/mysql --user=mysql --log-error=/var/log/mysqld.log --pid-file=/var/run/mysqld/mysqld.pid --socket=/var/lib/mysql/mysql.sock'
[ -e '/etc/SuSE-release' ] && cmdline='/usr/sbin/mysqld --basedir=/usr --datadir=/var/lib/mysql --user=mysql --pid-file=/var/lib/mysql/mysqld.pid --skip-external-locking --port=3306 --socket=/var/lib/mysql/mysql.sock'
lockfile='/var/run/mysqld/mysqld.pid'
[ -e '/etc/SuSE-release' ] && lockfile='/var/lib/mysql/mysqld.pid'


# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
