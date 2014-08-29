handler_desc='Checks the process state of the SMS gateway daemon.'
handler_exec()
{
  cmdline_rhel='/usr/sbin/smsd'
  cmdline_sles="$cmdline_rhel"
  lockfile_rhel='/var/run/smsd/smsd.pid'
  lockfile_sles="$lockfile_rhel"
  max='2'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
