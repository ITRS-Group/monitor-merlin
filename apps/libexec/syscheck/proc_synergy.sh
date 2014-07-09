handler_desc='Checks the process state of the op5 Business Services daemon.'
handler_exec()
{
  cmdline_rhel='/usr/bin/lua /opt/synergy/bin/synergy --monitor'
  cmdline_sles='/usr/bin/lua5.1 /opt/synergy/bin/synergy --monitor'
  lockfile_rhel='/opt/synergy/var/run/processor.run'
  lockfile_sles="$lockfile_rhel"
  max='1'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
