#!/bin/bash

# make sure $d is a valid path and source some generic functions
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
. "$d/bash/inc.sh"

[ "$1" == '--help' ] && msgdie '0' \
  'Checks for badly mounted filesystems and non-clean ext filesystems.'

depchk cut grep tune2fs

msg=''
while read -r dev mnt fs opts _; do
  [[ $fs =~ ^(ext[234]|tmpfs|xfs)$ ]] || continue

  bad=''
  while read -d, -r opt; do
    [[ $opt =~ ^noexec|ro$ ]] || continue
    bad+="${bad:+,}$opt"
  done <<< "$opts,"

  [ -n "$bad" ] && \
    msg+="${msg:+, }$dev ($fs) is mounted with $bad opt at $mnt"


  [[ $fs =~ ^ext[234]$ ]] || continue

  re='^Filesystem state:'
  read -r state < <(tune2fs -l "$dev" 2>&1 | grep "$re" | cut -d: -f2)

  [ "$state" == 'clean' ] || \
    msg+="${msg:+, }$dev is not clean"

done < /proc/mounts

[ -n "$msg" ] && {
  dieplug '2' "$msg"
} || {
  dieplug '0' "No problems detected."
}
