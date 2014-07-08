handler_desc='Checks the process state of the op5 Monitor core daemon.'
handler_exec()
{
  cmdline_rhel='/opt/monitor/bin/monitor -d /opt/monitor/etc/nagios.cfg'
  cmdline_sles="$cmdline_rhel"
  lockfile_rhel='/opt/monitor/var/nagios.lock'
  lockfile_sles="$lockfile_rhel"
  max='2'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
