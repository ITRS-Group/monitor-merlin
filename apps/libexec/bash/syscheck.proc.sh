#!/bin/bash

# 1. Fetch PID from $lockfile file.
# 2. Is there a process running with this PID?
# 3. Does the command line of this process match the expected cmdline?
chk_lockfile()
{
  local cmd cmdfile pid

  # keep original stderr redirection in fd3 and disable stderr
  exec 3>&2 2>&-

  [ -f "$lockfile" ] || \
    dieplug '2' "Process not running? Lock file not found ($lockfile)."
  [ -r "$lockfile" ] || \
    dieplug '3' "Lock file ($lockfile) is not readable."
  read -r pid < "$lockfile"
  [ -n "$pid" ] || \
    dieplug '3' "Failed reading lock file ($lockfile)."
  [[ $pid =~ ^[0-9]+$ ]] || \
    dieplug '3' "Invalid content in lock file ($lockfile)."

  cmdfile="/proc/$pid/cmdline"
  [ -f "$cmdfile" ] || \
    dieplug '2' "PID ($pid) read from lock file does not exist."
  [ -r "$cmdfile" ] || \
    dieplug '3' "cmdline file ($cmdfile) is not readable."
  IFS='' read -d'' -r cmd < "$cmdfile"
  [ -n "$cmd" ] || \
    dieplug '3' "Failed reading cmdline file ($cmdfile)."
  [ "${cmdline%% *}" == "$cmd" ] || \
    dieplug '2' "PID ($pid) read from lock file does not belong to process."

  # re-enable stderr and close fd3
  exec 2>&3 3>&-
}

# Finds the number of processes that are matching the specified cmdline
# using pgrep.
chk_numprocs()
{
  local msg num output pids

  #num="$(pgrep -fxc "$cmdline" 2>&1)" <- -c found in procps-ng

  num="$(pgrep -fx "$cmdline" 2>/dev/null | wc -l)"
  if ! [ "$num" -ge '0' ] &> /dev/null; then
    msg='pgrep returned unexpected output'
    res='3'
  elif [ "$num" == '0' ]; then
    msg="0 processes found.|procs=0"
    res='2'
  elif [ "$num" -gt "$max" ]; then
    msg="$num process(es) found (expected max $max).|procs=$num"
    res='1'
  else
    msg="$num process(es) found.|procs=$num"
    res='0'
  fi

  dieplug "$res" "$msg"
}

# Main function.
chkproc()
{
  [ -n "$lockfile" ] && chk_lockfile
  chk_numprocs
}

# Syntax helper function.
syntax_proc()
{
  msgdie '0' "[--info]
Verifies process *${procname//_/ }*
"
}

# Function handling the --info argument.
info_chkproc()
{
  local info info_lock info_max

  [ -n "$lockfile" ] && \
    info_lock="
Process lock file:
  $lockfile
"

  [ "$max" == '1' ] && {
    info_max='Expecting to find a single process running with this command line.'
  } || {
    info_max="Expecting to find up to $max processes running with this command line."
  }


  info="- *$procname* process check -
$info_lock
Matching process command line:
  $cmdline

$info_max
"

  printf '%s\n' "$info"

  exit 0
}


# source the ordinary generic functions
. "$d/bash/inc.sh"

# set up $procname for syntax_proc() and info_chkproc(),
# based on the $0 argument
procname="${0##*syscheck.proc_}"
procname="${procname%.sh}"

# we gotta know what sort of command line to look for
[ -n "$cmdline" ] || \
  dieplug '3' "Missing cmdline."

# max number of processes accepted until returning a WARNING state;
# default to no more than 1.
max="${max:-1}"


[ "$1" == '--help' ] && syntax_proc
[ "$1" == '--info' ] && info_chkproc

# can't run chk_numprocs() without these executables
depchk 'pgrep' 'wc'

# gogogo
chkproc
