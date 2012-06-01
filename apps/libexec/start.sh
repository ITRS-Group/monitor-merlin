#!/bin/sh

if mon node show | /usr/xpg4/bin/grep -q ^TYPE=master; then
	# we have masters, so we're a poller
	# Disable synergy nagios stuff (fugly workaround)
	ncfg=/opt/monitor/etc/nagios.cfg
	test -f $ncfg && sed -i '#^cfg_dir=/opt/monitor/etc/synergy#d' $ncfg
fi

/etc/init.d/merlind start
/etc/init.d/monitor start
