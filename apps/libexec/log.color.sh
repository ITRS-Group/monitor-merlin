#!/bin/bash

# sane defaults
IFS=$' \t\n'
LANG='C'

# find base dir and include common bash functions...
if [ "$0" == "${0//\//}" ]; then # no slashes in $0?
  base_dir='.'
else
  base_dir="${0%/*}"
fi
inc="$base_dir/bash/inc.sh"
[ -f "$inc" ] || { printf 'File not found: %s\n' "$inc"; exit 1; }
[ -r "$inc" ] || { printf 'File not readable: %s\n' "$inc"; exit 1; }
. "$inc"
unset inc



syntax()
{
  msgdie "${1:-1}" 'Usage: op5 log color [-a] <PATTERN>..

Colorizes data passed on stdin based on the given PATTERNs.

A PATTERN is a color prefix + a "grep -P" compatible regex.

The color prefix syntax is "[@]<color_name>:", i.e.
* An optional at character (@), making the color bold/bright.
* The name of a color (red/green/yellow/blue/magenta/cyan).
* A colon character (:).

In case the "-a" argument is given, the color prefix is not
expected - PATTERN will then be treated as a pure regex, and
unique colors will be applied automatically. In this mode, a
maximum of 12 PATTERNs can be given.

Examples:

cat /var/log/op5/merlin/logs/{daemon,neb}.log | sort -k 1,1 -s | \
  op5 log color \
    green:"^.*-> STATE_CONNECTED.*$" \
    yellow:"^.*-> STATE_PENDING.*$" \
    yellow:"^.*-> STATE_NEGOTIATING.*$" \
    red:"^.* -> STATE_NONE.*$" \
      | less -r

grep -e ALERT -e NOTIFICATION /opt/monitor/var/nagios.log | \
  mon log color -a host01 host02 host03 host04
'
}


clookup()
{
  case "$1" in
    red)      return 31  ;;
    green)    return 32  ;;
    yellow)   return 33  ;;
    blue)     return 34  ;;
    magenta)  return 35  ;;
    cyan)     return 36  ;;
    *)        return 100 ;;
  esac
}
cgrep()
{
  local bright color pattern
  color="${1%%:*}"
  pattern="${1#*:}"

  if [ -z "$color" -o -z "$pattern" ]; then
    printf 'Invalid argument (%s).\n' "$1"
    return 1
  fi

  if [ "${color:0:1}" == '@' ]; then
    color="${color:1}"
    bright='1;'
  fi

  # get color code via return code
  clookup "$color"
  # * run grep and set the color via environ.
  # * match all lines via |$ in the grep expression, but mark/colorize
  #   only the interesting ones (as the $pattern describes).
  # * pipe the grep output to cgrep() itself in case there are any
  #   additional patterns available (post argument-shift), otherwise
  #   just pipe to cat and be done with it.
  GREP_COLORS= GREP_COLOR="$bright$?" grep -P --color=always -e "($pattern|$)" | {
    shift
    [ -n "$1" ] && { cgrep "$@" || exit $?; } || cat
  }

}

if [ "$1" == '--help' -o "$1" == '-h' ]; then
  syntax '0'
fi

# hidden feature: print the main functions to allow easy copy-paste into other
# shells, since they can be used by themselves without this wrapping.
if [ "$1" == '--type' -o "$1" == '-t' ]; then
  type clookup cgrep | grep -v 'is a function$'
  exit 0
fi

if [ "$1" == '-a' ]; then
  auto='yes'
  shift
else
  auto='no'
fi

# at this point, some patterns are expected
if [ -z "$1" ]; then
  syntax '1'
fi

if [ "$auto" == 'yes' ]; then
  [ "$#" -gt '12' ] && \
    msgdie '1' "Too many arguments in auto coloring mode ($#>12)."

  color=(red green yellow blue magenta cyan)
  args=()
  n='0'
  p=''
  # iterate the positional arguments ($1, $2, etc.), i.e. the patterns
  for pattern; do
    # re-use colors if needed, but bold (@ prefix)
    [ "$n" == '6' ] && { n='0'; p='@'; }
    args+=("${p}${color[$n]}:${pattern}")
    let n++
  done

  # replace/reset the positional arguments using the $args array
  set -- "${args[@]}"
fi

cgrep "$@"
exit $?
