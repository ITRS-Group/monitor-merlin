#!/bin/bash

# make sure $d is a valid path and source some generic functions
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
. "$d/bash/inc.sh"

[ "$1" == '--help' ] && syntax '
Detects errors in the merlin and nacoma database using mysqlcheck.'

depchk grep mysqlcheck


if out="$(mysqlcheck -q -s --databases merlin nacoma 2>&1)"; then
  errors="$(grep -E -i -B1 '^error[[:space:]]*:' <<< "$out")"
  if [ -z "$errors" ]; then
    msg='No errors found.'
    res='0'
  else
    msg='Error(s) detected.'
    msg+=$'\n'"$errors"
    res='2'
  fi
else
  msg='Failed unexpectedly.'
  msg+=$'\n'"COMMAND(pgrep -fx \"$cmdline\")"
  msg+=$'\n''==OUTPUT START=='
  msg+=$'\n'"$out"
  msg+=$'\n''==OUTPUT END=='
  res='3'
fi

diecho "$res" "$msg"
