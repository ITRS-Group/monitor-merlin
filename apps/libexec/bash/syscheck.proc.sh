#!/bin/bash

# SNTX: <cmdline=line> <lockfile=file> chk_lockfile
# DESC: Thoroughly verifies a lockfile/pidfile. See comments below.
chk_lockfile()
{
  local cmd cmdfile pid

  # Is the lockfile a file?
  [ -f "$lockfile" ] || \
    dieplug '2' "Process not running? Lock file not found ($lockfile)."
  # Is the lockfile readable?
  [ -r "$lockfile" ] || \
    dieplug '3' "Lock file ($lockfile) is not readable."

  # Read the first line of the lockfile into $pid.
  read -r pid < "$lockfile"
  # Is $pid empty?
  [ -n "$pid" ] || \
    dieplug '3' "Lock file ($lockfile) empty?"
  # Does $pid look like a PID?
  [[ $pid =~ ^[0-9]+$ ]] || \
    dieplug '3' "Invalid content in lock file ($lockfile)."

  cmdfile="/proc/$pid/cmdline"
  # Is the cmdline a file? (Directory is probably missing if not => No process.)
  [ -f "$cmdfile" ] || \
    dieplug '2' "PID ($pid) from lock file ($lockfile) not found."
  # Is the cmdline file readable?
  [ -r "$cmdfile" ] || \
    dieplug '3' "cmdline file ($cmdfile) is not readable."

  # Read all data until the first null byte (the cmd name/argv[0]) into $cmd.
  read -d '' -r cmd < "$cmdfile"
  # Is $cmd empty?
  [ -n "$cmd" ] || \
    dieplug '3' "Failed reading cmdline file ($cmdfile)."

  # Is the cmd name of this process the same as in the given cmdline
  [ "${cmdline%% *}" == "$cmd" ] || \
    dieplug '2' "PID ($pid) from lock file ($lockfile) not belonging to expected process."
}

# SNTX: <cmdline=line> chk_numprocs
# DESC: Finds the number of processes that are matching the specified cmdline.
chk_numprocs()
{
  local msg num output pids

  #num="$(pgrep -fxc "$cmdline" 2>&1)" <- -c found in procps-ng

  num="$(pgrep -fx "$cmdline" 2>/dev/null | wc -l)"
  if ! [ "$num" -ge '0' ] &> /dev/null; then
    dieplug '3' 'pgrep returned unexpected output'
  elif [ "$num" == '0' ]; then
    dieplug '2' "0 processes found.|procs=0"
  elif [ "$num" -gt "$max" ]; then
    dieplug '1' "$num process(es) found (expected max $max).|procs=$num"
  else
    dieplug '0' "$num process(es) found.|procs=$num"
  fi
}

# SNTX: <procname=name> syntax_proc
# DESC: Prints help text and exits.
syntax_proc()
{
  msgdie '0' "[--info]
Verifies process *${procname//_/ }*
"
}

# SNTX: <cmdline=line> [lockfile=file] <procname=name> <max=num> info_proc
# DESC: Prints verbose information about the current process check and exits.
info_proc()
{
  local info info_lock info_max

  if [ -n "$lockfile" ]; then
    info_lock="
Process lock file:
  $lockfile
"
  fi

  if [ "$max" == '1' ]; then
    info_max='Expecting to find a single process running with this command line.'
  else
    info_max="Expecting to find up to $max processes running with this command line."
  fi


  info="- *$procname* process check -
$info_lock
Matching process command line:
  $cmdline

$info_max
"

  printf '%s\n' "$info"

  exit 0
}


# Set up $procname for info_proc(), based on the $0 argument:
# * Strip the longest prefixing match of *syscheck.proc_
procname="${0##*syscheck.proc_}"
# * Strip the shortest suffixing match of %.sh
procname="${procname%.sh}"

# A process command line must be specified.
[ -n "$cmdline_rhel" ] || \
  dieplug '3' "Missing RHEL command line."
[ -n "$cmdline_sles" ] || \
  dieplug '3' "Missing SLES command line."

# Translate OS suffixed variables into non-suffixed ones, depending on OS.
if is_rhel; then
  cmdline="$cmdline_rhel"
  [ -n "$lockfile_rhel" ] && lockfile="$lockfile_rhel"
elif is_sles; then
  cmdline="$cmdline_sles"
  [ -n "$lockfile_sles" ] && lockfile="$lockfile_sles"
else
  dieplug '3' 'Unable to determine the current operating system.'
fi

# The WARNING threshold of the maximum matching number of processes.
# Default to no more than 1.
max="${max:-1}"


# Display helptext/info of this check?
[ "$1" == '--help' -o "$1" == '-h' ] && syntax_proc
[ "$1" == '--info' ] && info_proc

# chk_numprocs() requires these executables to function.
depchk 'pgrep' 'wc'


# Verify the lockfile if any.
[ -n "$lockfile" ] && chk_lockfile

# Verify the number of matching processes.
chk_numprocs
