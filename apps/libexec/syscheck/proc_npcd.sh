handler_desc='Checks the process state of the performance data processing daemon.'
handler_exec()
{
  cmdline_rhel='/opt/monitor/bin/npcd -d -f /opt/monitor/etc/pnp/npcd.cfg'
  cmdline_sles="$cmdline_rhel"
  lockfile_rhel='/var/run/npcd.pid'
  lockfile_sles="$lockfile_rhel"
  max='1'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
