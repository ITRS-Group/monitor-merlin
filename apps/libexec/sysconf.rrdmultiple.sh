#/bin/bash
set -e

if [ "$UID" -gt 0 ]; then
	echo "You need to run this as root"
	exit 1;
fi

service npcd stop || :
service rrdcached stop || :
pushd /opt/monitor/op5/pnp
chown -R monitor:apache perfdata
tar czf perfdata-backup.tar.gz perfdata
service rrdcached start
su monitor -c 'for check in $(./rrd_convert.pl --cfg_dir=/opt/monitor/etc/pnp --list_commands|awk "{print \$2 }"); do yes|./rrd_convert.pl --cfg_dir=/opt/monitor/etc/pnp --check_command=$check --no_structure_check;done'
sed -i -e 's/RRD_STORAGE_TYPE = SINGLE/RRD_STORAGE_TYPE = MULTIPLE/' /opt/monitor/etc/pnp/process_perfdata.cfg
find perfdata -name '*.xml' -exec rm {} \;
popd
service rrdcached start
service npcd start
