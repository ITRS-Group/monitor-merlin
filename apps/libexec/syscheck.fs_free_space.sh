#!/bin/bash

# make sure $d is a valid path and source some generic functions
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
. "$d/bash/inc.sh"

[ "$1" == '--help' ] && msgdie '0' \
  'Checks all locally mounted filesystems for low amounts of free space.'

PATH="/opt/plugins:$PATH"
depchk check_disk

check_disk -w 10% -c 5% -L -A -v
