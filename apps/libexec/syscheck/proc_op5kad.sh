handler_desc='Checks the process state of the op5 Keepalive daemon.'
handler_exec()
{
  cmdline_rhel='/usr/bin/python /usr/bin/op5kad -c /etc/op5kad/kad.conf'
  cmdline_sles="$cmdline_rhel"
  lockfile_rhel='/var/run/op5kad.pid'
  lockfile_sles="$lockfile_rhel"
  max='1'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
