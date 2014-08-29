#!/bin/bash

# (un)set sane defaults
unset TZ
IFS=$' \t\n'
LANG='C'

syntax()
{
  msgdie "${1:-3}" 'Usage: op5 sysconf check [OPTION].. [HANDLER]..

If no handler names given, all available handlers will be run.

 -c, --complete  Output all check results, regardless of state.
 -h, --help      This syntax text.
 -l, --list      List all available handlers.
 -v, --verbose   Output full multi line check results.
'
}

handlers_get()
{
  local f fbase name
  handler_name=()
  handler_path=()
  for f in $1/*.sh; do
    [ -f "$f" ] || continue
    [ -r "$f" ] || continue
    fbase="${f##*/}"
    name="${fbase%.sh}"
    handler_name+=("$name")
    handler_path+=("$f")
  done
}

handlers_list()
{
  local n name path
  for n in ${!handler_name[@]}; do
    name="${handler_name[$n]}"
    path="${handler_path[$n]}"
    . "$path"
    if [ "$verbose" == 'yes' ]; then
      printf '[%s]' "$path"
    else
      printf '(%s)' "$name"
    fi
    printf '\n  %s\n\n' "$handler_desc"
  done
}

handlers_run()
{
  handler_path()
  {
    local n
    for n in ${!handler_name[@]}; do
      path="${handler_path[$n]}"
      [ "${handler_name[$n]}" == "$1" ] && return 0
    done
    return 1
  }

  local handler_out handler_ret line name path
  main_out=''
  main_ret='0'

  for name; do
    # get file path to handler, and check if the handler exists at the same time
    handler_path "$name" || dieplug '3' "No such handler: $name"

    # add dummy function in case not sourced from handler later
    handler_exec() { dieplug '3' 'Invalid handler.'; }
    . "$path"

    # run handler and fetch its output and return code
    handler_out="$(handler_exec 2>&1)"
    handler_ret="$?"

    # only a single handler to be run? then we're all done
    [ "$#" == '1' ] && msgdie "$handler_ret" "$handler_out"

    # ignore successful handlers unless running in complete mode
    [ "$handler_ret" == '0' -a "$complete" == 'no' ] && continue

    # increase main exit code if handler's code was more severe
    [ "$handler_ret" -gt "$main_ret" ] && main_ret="$handler_ret"

    if [ "$verbose" == 'yes' ]; then
      # append to output: handler name + handler output + newline
      main_out+="($name)"$'\n'"$handler_out"$'\n\n'
    else
      # get first line of handler output
      line="${handler_out%%$'\n'*}"

      # strip perfdata from line (remove shortest suffixing match of |*)
      line="${line%|*}"

      # append to output: handler name + first line of handler output + newline
      main_out+="($name) $line"$'\n'
    fi
  done
}


# isn't there any slashes in $0? then it was run as "bash sysconf.check.sh"
if [ "$0" == "${0//\//}" ]; then
  base_dir='.'
else
  base_dir="${0%/*}"
fi
# that was just like base_dir="$(dirname -- "$0")" would have been, but bash :3

# include common bash functions
inc="$base_dir/bash/inc.sh"
[ -f "$inc" ] || { printf 'File not found: %s\n' "$inc"; exit 3; }
[ -r "$inc" ] || { printf 'File not readable: %s\n' "$inc"; exit 3; }
. "$inc"
unset inc


# set sane argument defaults
handler_run=()
complete='no'
list='no'
verbose='no'

# parse command line arguments
for arg; do
  case "$arg" in
    '-c'|'--complete')
      complete='yes'
      ;;
    '-h'|'--help')
      syntax 0
      ;;
    '-l'|'--list')
      list='yes'
      ;;
    '-v'|'--verbose')
      verbose='yes'
      ;;
    *)
      handler_run+=("$arg")
      ;;
  esac
done


# get available handlers
handlers_dir="$base_dir/syscheck"
handlers_get "$handlers_dir"
if [ "${#handler_name[@]}" == '0' ]; then
  dieplug '3' "Unable to find any handlers at $handlers_dir/*.sh"
fi

# asked to list handlers?
if [ "$list" == 'yes' ]; then
  handlers_list "${handler_name[@]}"
  exit 0
fi

# run handlers given on cmdline, or otherwise all available ones
if [ "${#handler_run[@]}" -gt '0' ]; then
  handlers_run "${handler_run[@]}"
else
  handlers_run "${handler_name[@]}"
fi

# construct final output msg
if [ "$main_ret" == '0' ]; then
  msg='No system problems detected.'
else
  msg='System problems detected.'
fi
[ -n "$main_out" ] && msg+=$'\n\n'"$main_out"

# print final exit status msg and exit...
dieplug "$main_ret" "$msg"
