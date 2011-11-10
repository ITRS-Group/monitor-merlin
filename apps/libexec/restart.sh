#!/bin/sh

if /etc/init.d/monitor configtest; then
	mon stop
	mon start
else
	echo "Refusing to restart monitor with a flawed configuration"
	exit 1
fi
