#!/bin/bash

cmdline_rhel='/usr/bin/postmaster -p 5432 -D /var/lib/pgsql/data'
cmdline_sles='/usr/lib/postgresql84/bin/postgres -D /var/lib/pgsql/data'
lockfile_rhel='/var/run/postmaster.5432.pid'
lockfile_sles='/var/lib/pgsql/data/postmaster.pid'
max='1'

# make sure $d is a valid path
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
# continue processing by sourcing the generic proc script
. "$d/bash/syscheck.proc.sh"
