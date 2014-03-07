#!/bin/bash

# make sure $d is a valid path and source some generic functions
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
. "$d/bash/inc.sh"

[ "$1" == '--help' ] && msgdie '0' \
  'Detects errors in the merlin and nacoma database using mysqlcheck.'

depchk grep mysqlcheck

# Sourced cmd_exec() will execute the given command line and store the output
# text in $OUT. Should the command not return zero, the script will exit.
cmd_exec mysqlcheck -q -s --databases merlin nacoma

errors="$(printf '%s\n' "$OUT" | grep -Ei -B1 '^error[[:space:]]*:')"

if [ -z "$errors" ]; then
  ret='0'
  msg='No errors detected.'
else
  ret='2'
  msg='Error(s) detected.'
  msg+=$'\n'"$OUT"
fi

dieplug "$ret" "$msg"
