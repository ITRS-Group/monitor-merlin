#!/bin/sh

if [ "`uname`" = "SunOS" ]; then
	/opt/monitor/svc/svc-monitor slay
	svcadm disable -t op5monitor
	svcadm disable -ts op5merlin
else
	/etc/init.d/monitor stop
	/etc/init.d/monitor slay
	/etc/init.d/merlind stop
fi
