handler_desc='Checks the process state of the op5 Trapper processor daemon.'
handler_exec()
{
  cmdline_rhel='/usr/bin/processor -c /etc/processor/processor.conf'
  cmdline_sles="$cmdline_rhel"
  lockfile_rhel='/opt/trapper/var/run/processor.run'
  lockfile_sles="$lockfile_rhel"
  max='1'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
