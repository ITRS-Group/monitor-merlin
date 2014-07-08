handler_desc='Checks the process state of the op5 Merlin daemon.'
handler_exec()
{
  cmdline_rhel='/usr/bin/merlind -c /opt/monitor/op5/merlin/merlin.conf'
  cmdline_sles="$cmdline_rhel"
  lockfile_rhel='/var/run/merlin.pid'
  lockfile_sles="$lockfile_rhel"
  max='1'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
