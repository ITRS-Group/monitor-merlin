#!/bin/sh

if [ "`uname`" = "SunOS" ]; then
	grep=/usr/xpg4/bin/grep
	gsed=/opt/csw/bin/gsed
	if mon node show | $grep -q ^TYPE=master; then
		# we have masters, so we're a poller
		# Disable synergy nagios stuff (fugly workaround)
		ncfg=/opt/monitor/etc/nagios.cfg
		test -f $ncfg && $gsed -i '#^cfg_dir=/opt/monitor/etc/synergy#d' $ncfg
	fi

	svcadm enable -ts op5merlin
	svcadm enable -ts op5monitor
else
	if mon node show | grep -q ^TYPE=master; then
		# we have masters, so we're a poller
		# Disable synergy nagios stuff (fugly workaround)
		ncfg=/opt/monitor/etc/nagios.cfg
		test -f $ncfg && sed -i '#^cfg_dir=/opt/monitor/etc/synergy#d' $ncfg
	fi

	/etc/init.d/merlind start
	/etc/init.d/monitor start
fi
