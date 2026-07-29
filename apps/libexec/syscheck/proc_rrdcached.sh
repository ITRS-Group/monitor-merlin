handler_desc='Checks the process state of the RRD caching daemon.'
handler_exec()
{
  lockfile_rhel='/opt/monitor/var/rrdtool/rrdcached/rrdcached.pid'
  lockfile_sles="$lockfile_rhel"
  max='1'

  _rrdcached_cmdline_from_pid() {
    local pid="$1" raw

    [ -n "$pid" ] || return 1
    [ -r "/proc/$pid/cmdline" ] || return 1
    raw="$(tr '\0' ' ' < "/proc/$pid/cmdline")"
    # Trim trailing whitespace left by the final NUL.
    raw="${raw%"${raw##*[![:space:]]}"}"
    [ -n "$raw" ] || return 1
    printf '%s' "$raw"
  }

  # Match the live argv so flag changes (e.g. op5-rrdtool-config drop-in)
  # do not break pgrep -fx in syscheck.proc.sh.
  cmdline_rhel=
  if [ -r "$lockfile_rhel" ]; then
    read -r _pid < "$lockfile_rhel"
    cmdline_rhel="$(_rrdcached_cmdline_from_pid "$_pid")"
  fi
  if [ -z "$cmdline_rhel" ]; then
    _pid="$(systemctl show -p MainPID --value rrdcached.service 2>/dev/null)"
    [ "$_pid" != '0' ] && cmdline_rhel="$(_rrdcached_cmdline_from_pid "$_pid")"
  fi
  if [ -z "$cmdline_rhel" ]; then
    cmdline_rhel='/usr/bin/rrdcached -g -l unix:/opt/monitor/var/rrdtool/rrdcached/rrdcached.sock -b /opt/monitor/op5/pnp/perfdata -B -R -p /opt/monitor/var/rrdtool/rrdcached/rrdcached.pid'
  fi
  cmdline_sles="$cmdline_rhel"

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
