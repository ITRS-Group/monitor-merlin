#!/bin/bash
BASEDIR="/opt/monitor/etc"

check_result_path="/dev/shm/monitor/var/spool/checkresults"
service_perfdata_file="/dev/shm/monitor/var/service-perfdata"
host_perfdata_file="/dev/shm/monitor/var/host-perfdata"
perfdata_spool_dir="/dev/shm/monitor/var/spool/perfdata/"
process_host_perfdata='command_line                   /bin/mv /dev/shm/monitor/var/host-perfdata /dev/shm/monitor/var/spool/perfdata/host_perfdata.$TIMET$'
process_service_perfdata='command_line                   /bin/mv /dev/shm/monitor/var/service-perfdata /dev/shm/monitor/var/spool/perfdata/service_perfdata.$TIMET$'

usage()
{
    cat << END_OF_HELP
<enable>
Configures the Monitor system to use ramdisk (tmpfs) for temporary
performance data files.

NOTE: This operation is potentially dangerous in case the system
has a limited amount of RAM. Due to this the changes will not take
effect until the command is run with the "enable" argument.

END_OF_HELP
}

case "$1" in
	enable)
		;; # do nothing
	*)
		usage
		exit 0
esac


#========= nagios.cfg =========
egrep -E "^check_result_path=" ${BASEDIR}/nagios.cfg > /dev/null
ret=$?
if [ $ret == 0 ]; then
	sed -i "s|^check_result_path=.*|check_result_path=${check_result_path}|" ${BASEDIR}/nagios.cfg
else
	echo "WARNING: Missing variable: check_result_path in ${BASEDIR}/nagios.cfg"
	echo "Add the following to ${BASEDIR}/nagios.cfg"
	echo "check_result_path=${check_result_path}"
	echo ""
fi

egrep -E "^service_perfdata_file=" ${BASEDIR}/nagios.cfg > /dev/null
ret=$?
if [ $ret == 0 ]; then
	sed -i "s|^service_perfdata_file=.*|service_perfdata_file=${service_perfdata_file}|" ${BASEDIR}/nagios.cfg
else
	echo "WARNING: Missing variable: service_perfdata_file in ${BASEDIR}/nagios.cfg"
	echo "Add the following to ${BASEDIR}/nagios.cfg"
	echo "service_perfdata_file=${service_perfdata_file}"
	echo ""
fi

egrep -E "^host_perfdata_file=" ${BASEDIR}/nagios.cfg > /dev/null
ret=$?
if [ $ret == 0 ]; then
	sed -i "s|^host_perfdata_file=.*|host_perfdata_file=${host_perfdata_file}|" ${BASEDIR}/nagios.cfg
else
	echo "WARNING: Missing variable: host_perfdata_file in ${BASEDIR}/nagios.cfg"
	echo "Add the following to ${BASEDIR}/nagios.cfg"
	echo "host_perfdata_file=${host_perfdata_file}"
	echo ""
fi

#========= npcd.cfg =========
egrep -E "^perfdata_spool_dir =" ${BASEDIR}/pnp/npcd.cfg > /dev/null
ret=$?
if [ $ret == 0 ]; then
	sed -i "s|^perfdata_spool_dir =.*|perfdata_spool_dir = ${perfdata_spool_dir}|" ${BASEDIR}/pnp/npcd.cfg
else
	echo "WARNING: Missing variable: perfdata_spool_dir in ${BASEDIR}/pnp/npcd.cfg"
	echo "Add the following to ${BASEDIR}/pnp/npcd.cfg"
	echo "perfdata_spool_dir = ${perfdata_spool_dir}"
	echo ""
fi

#========= misccommands.cfg =========
if [ -f ${BASEDIR}/misccommands.cfg ]; then
	egrep -E 'command_line                   /bin/mv /opt/monitor/var/service-perfdata /opt/monitor/var/spool/perfdata/service_perfdata.\$TIMET\$' ${BASEDIR}/misccommands.cfg > /dev/null
	ret=$?
	if [ $ret == 0 ]; then
		sed -i "s|command_line                   /bin/mv /opt/monitor/var/service-perfdata /opt/monitor/var/spool/perfdata/service_perfdata.\\\$TIMET\\\$|${process_service_perfdata}|" ${BASEDIR}/misccommands.cfg
	else
		egrep -E 'command_line                   /bin/mv /dev/shm/monitor/var/service-perfdata /dev/shm/monitor/var/spool/perfdata/service_perfdata.\$TIMET\$' ${BASEDIR}/misccommands.cfg > /dev/null
		ret=$?
		if [ $ret != 0 ]; then
			echo "WARNING: non standard configured variable: 'process-service-perfdata' in ${BASEDIR}/misccommands.cfg"
			echo "update it to look like below:"
			echo "${process_service_perfdata}"
			echo ""
		fi
	fi

	egrep -E 'command_line                   /bin/mv /opt/monitor/var/host-perfdata /opt/monitor/var/spool/perfdata/host_perfdata.\$TIMET\$' ${BASEDIR}/misccommands.cfg > /dev/null
	ret=$?
	if [ $ret == 0 ]; then
		sed -i "s|command_line                   /bin/mv /opt/monitor/var/host-perfdata /opt/monitor/var/spool/perfdata/host_perfdata.\\\$TIMET\\\$|${process_host_perfdata}|" ${BASEDIR}/misccommands.cfg
	else
		egrep -E 'command_line                   /bin/mv /dev/shm/monitor/var/host-perfdata /dev/shm/monitor/var/spool/perfdata/host_perfdata.\$TIMET\$' ${BASEDIR}/misccommands.cfg > /dev/null
		ret=$?
		if [ $ret != 0 ]; then
			echo "WARNING: non standard configured variable: 'process-host-perfdata' in ${BASEDIR}/misccommands.cfg"
			echo "update it to look like below:"
			echo "${process_host_perfdata}"
			echo ""
		fi
	fi
else
	echo "WARNING: Couldn't find ${BASEDIR}/misccommands.cfg, if you are on a poller this is OK"
fi

#========= /etc/sysconfig/monitor =========
if [ -f /etc/sysconfig/monitor ]; then
	egrep -E 'USE_RAMDISK=.*' /etc/sysconfig/monitor > /dev/null
	ret=$?

	if [ $ret == 0 ]; then
		sed -i 's|USE_RAMDISK=.*|USE_RAMDISK=1|' /etc/sysconfig/monitor
	else
		echo 'USE_RAMDISK=1' >> /etc/sysconfig/monitor
	fi
else
	echo 'USE_RAMDISK=1' >> /etc/sysconfig/monitor
fi

echo "Your configuration is now configured to utilize ramdisk"
echo "Please run the following commands:"
echo "mon stop"
echo "Wait a few seconds to let ncpd take care of all the old data"
echo "service npcd restart"
echo "mon start"
