handler_desc='Checks the process state of the op5 Monitor core worker daemons'
handler_exec()
{
  cmdline_rhel='/opt/monitor/bin/monitor --worker /opt/monitor/var/rw/nagios.qh'
  cmdline_sles="$cmdline_rhel"
  lockfile_rhel=''
  lockfile_sles=''
  max='100'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
