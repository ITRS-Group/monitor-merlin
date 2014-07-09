handler_desc='Checks the process state of the op5 Portal configuration daemon.'
handler_exec()
{
  cmdline_rhel='/usr/bin/php -q /opt/op5sys/bin/config-daemon.php -p /var/run/op5config.pid -d'
  cmdline_sles="$cmdline_rhel"
  lockfile_rhel='/var/run/op5config.pid'
  lockfile_sles="$lockfile_rhel"
  max='1'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
