#!/bin/sh

if mon node show | grep -q ^TYPE=master; then
	# we have masters, so we're a poller
	# Disable synergy nagios stuff (fugly workaround) (but run sed only if needed)
	ncfg=/opt/monitor/etc/nagios.cfg
	test -f $ncfg && \
		grep -q '^cfg_dir=/opt/monitor/etc/synergy' $ncfg && \
		sed -i '/^cfg_dir=\/opt\/monitor\/etc\/synergy/d' $ncfg
fi

/etc/init.d/merlind start
/etc/init.d/monitor start
