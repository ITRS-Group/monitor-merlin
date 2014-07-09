handler_desc='Checks the process state of the syslog-ng daemon.'
handler_exec()
{
  cmdline_rhel='/sbin/syslog-ng -p /var/run/syslog-ng.pid'
  cmdline_sles="$cmdline_rhel"
  lockfile_rhel='/var/run/syslog-ng.pid'
  lockfile_sles="$lockfile_rhel"
  max='1'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
