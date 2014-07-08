handler_desc='Checks the process state of the PostgreSQL database server.'
handler_exec()
{
  cmdline_rhel='/usr/bin/postmaster -p 5432 -D /var/lib/pgsql/data'
  cmdline_sles='/usr/lib/postgresql84/bin/postgres -D /var/lib/pgsql/data'
  lockfile_rhel='/var/run/postmaster.5432.pid'
  lockfile_sles='/var/lib/pgsql/data/postmaster.pid'
  max='1'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
