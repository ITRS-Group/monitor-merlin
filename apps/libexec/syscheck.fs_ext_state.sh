#!/bin/bash

# make sure $d is a valid path and source some generic functions
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
. "$d/bash/inc.sh"

[ "$1" == '--help' ] && msgdie '0' \
  'Checks if any mounted ext-based filesystems are non-clean.'

depchk cut grep tune2fs

msg=''
while read -r dev mnt fs opts _; do
  [[ $fs =~ ^ext[234]$ ]] || continue

  read -r state < \
    <(tune2fs -l "$dev" 2>&1 | grep '^Filesystem state:' | cut -d: -f2)

  [ "$state" == 'clean' ] || \
    msg+=", $dev is not clean"

done < /proc/mounts

if [ -n "$msg" ]; then
  dieplug '2' "${msg:2}" # Strip first two characters of msg (, ).
else
  dieplug '0' "No problems detected."
fi
