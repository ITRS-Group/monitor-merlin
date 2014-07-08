handler_desc='Checks the process state of the cron daemon.'
handler_exec()
{
  cmdline_rhel='crond'
  cmdline_sles='/usr/sbin/cron'
  lockfile_rhel='/var/run/crond.pid'
  lockfile_sles='/var/run/cron.pid'
  max='1'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
