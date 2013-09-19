#!/bin/bash

# make sure $d is a valid path and source some generic functions
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
. "$d/bash/inc.sh"

[ "$1" == '--help' ] && syntax '
Checks all locally mounted filesystems for low amounts of free inodes.'

PATH="/opt/plugins:$PATH"
depchk check_disk

check_disk -W 10% -K 5% -L -A -v
