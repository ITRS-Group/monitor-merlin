handler_desc='Checks the process state of the Network Time Protocol daemon.'
handler_exec()
{
  cmdline_rhel='ntpd -u ntp:ntp -p /var/run/ntpd.pid -g'
  cmdline_sles='/usr/sbin/ntpd -p /var/run/ntp/ntpd.pid -g -u ntp:ntp -i /var/lib/ntp -c /etc/ntp.conf'
  lockfile_rhel='/var/run/ntpd.pid'
  lockfile_sles='/var/run/ntp/ntpd.pid'
  max='1'

  # continue processing by sourcing the generic proc script
  . "$base_dir/bash/syscheck.proc.sh"
}
