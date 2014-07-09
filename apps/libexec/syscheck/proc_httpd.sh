handler_desc='Checks the process state of the Apache HTTP server processes.'
handler_exec()
{
  cmdline_rhel='/usr/sbin/httpd'
  cmdline_sles='/usr/sbin/httpd2-prefork -f /etc/apache2/httpd.conf -DSSL'
  lockfile_rhel='/var/run/httpd/httpd.pid'
  lockfile_sles='/var/run/httpd2.pid'
  max='50'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
