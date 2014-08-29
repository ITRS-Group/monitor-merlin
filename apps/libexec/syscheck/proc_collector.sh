handler_desc='Checks the process state of the op5 Trapper collector daemon.'
handler_exec()
{
  cmdline_rhel='/opt/trapper/bin/collector -On -c /opt/trapper/etc/collector.conf -A -Lsd -p /opt/trapper/var/run/collector.run'
  cmdline_sles="$cmdline_rhel"
  lockfile_rhel='/opt/trapper/var/run/collector.run'
  lockfile_sles="$lockfile_rhel"
  max='1'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
