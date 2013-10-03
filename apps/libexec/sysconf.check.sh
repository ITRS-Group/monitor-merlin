#!/bin/bash

# (un)set sane defaults
unset TZ
IFS=$' \t\n'
LANG='C'

# show help text, especially for mon command
if [ "$1" == '--help' -o "$1" == '-h' ]; then
  printf '[-c|--complete]\n'
  printf 'Summarizes the results of all "mon syscheck" handlers.\n'
  printf 'The -c switch will output all check results, regardless of state.\n'

  exit 0
fi

# did the user ask to see all check results?
complete='no'
if [ "$1" == '--complete' -o "$1" == '-c' ]; then
  complete='yes'
fi

# fetch output of mon
if ! handlers="$(mon --help 2>&1)"; then
  printf 'UNKNOWN: Failed running "mon".\n\n%s\n' "$handlers"
  exit 3
fi

# find all submods
handlers="$(printf '%s\n' "$handlers" | \
  grep '^[[:space:]]*syscheck[[:space:]]*:' | cut -d: -f2 | sed 's/,/ /g')"

# make sure we actually found some sub commands
run='no'
for handler in $handlers; do
  run='yes'
  break
done
if [ "$run" != 'yes' ]; then
  printf 'UNKNOWN: No mon syscheck sub commands found.\n'
  exit 3
fi

# sane defaults
main_out=''
main_ret='0'

for handler in $handlers; do
  # run handler - not interesting if ok
  handler_out="$(mon syscheck $handler 2>&1)"

  # fetch handler exit code
  handler_ret="$?"

  [ "$handler_ret" == '0' -a "$complete" == 'no' ] && continue

  # increase main exit code if handler's code was more severe
  [ "$handler_ret" -gt "$main_ret" ] && main_ret="$handler_ret"

  # get first line of handler output
  read -r line <<< "$handler_out"

  # append to output: handler name + first line of handler output + newline
  main_out+="($handler) $line"$'\n'
done


# print final exit status msg and exit...

[ -n "$main_out" ] && main_out=$'\n'"$main_out"

if [ "$main_ret" == '0' ]; then
  printf 'OK: No system problems detected.\n%s' "$main_out"
else
  case "$main_ret" in
    1) prefix='WARNING' ;;
    2) prefix='CRITICAL' ;;
    3) prefix='UNKNOWN' ;;
    *) prefix="UNKNOWN ($main_ret)" main_ret='3' ;;
  esac

  printf '%s: System problems detected.\n%s' "$prefix" "$main_out"
fi

exit "$main_ret"
