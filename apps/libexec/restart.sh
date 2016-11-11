#!/bin/sh

if /usr/bin/naemon --verify-config /opt/monitor/etc/naemon.cfg; then
	mon stop
	mon start
else
	echo "Refusing to restart monitor with a flawed configuration"
	exit 1
fi
