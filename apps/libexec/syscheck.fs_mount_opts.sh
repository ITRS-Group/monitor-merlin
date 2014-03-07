#!/bin/bash

# make sure $d is a valid path and source some generic functions
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
. "$d/bash/inc.sh"

[ "$1" == '--help' ] && msgdie '0' \
  'Checks for possibly invalidly mounted filesystems.'

msg=''
while read -r dev mnt fs opts _; do
  [[ $fs =~ ^(ext[234]|tmpfs|xfs)$ ]] || continue

  bad=''
  while read -d, -r opt; do
    [[ $opt =~ ^noexec|nosuid|ro$ ]] || continue
    bad+=" $opt"
  done <<< "$opts,"

  [ -n "$bad" ] && \
    msg+=$'\n'"$dev ($fs) at $mnt (opts:$bad)"
done < /proc/mounts

if [ -n "$msg" ]; then
  dieplug '2' "Mount problems detected.$msg"
else
  dieplug '0' "No problems detected."
fi
