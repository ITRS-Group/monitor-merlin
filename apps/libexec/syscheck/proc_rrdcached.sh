handler_desc='Checks the process state of the RRD caching daemon.'
handler_exec()
{
  cmdline_rhel='/usr/bin/rrdcached -p /opt/monitor/var/rrdtool/rrdcached/rrdcached.pid -m 0777 -l unix:/opt/monitor/var/rrdtool/rrdcached/rrdcached.sock -b /opt/monitor/var/rrdtool/rrdcached -P FLUSH,PENDING -z 1800 -w 1800 -s root -j /opt/monitor/var/rrdtool/rrdcached/spool -p /opt/monitor/var/rrdtool/rrdcached/rrdcached.pid'
  cmdline_sles="$cmdline_rhel"
  lockfile_rhel='/opt/monitor/var/rrdtool/rrdcached/rrdcached.pid'
  lockfile_sles="$lockfile_rhel"
  max='1'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
