#!/bin/sh
#
# Start / stop the Merlin daemon
#
# chkconfig: 235 90 10
#
### BEGIN INIT INFO
# Provides: merlin
# Required-Start: $local_fs $network $remote_fs
# Required-Stop: $local_fs $network $remote_fs
# Default-Start:  2 3 5
# Default-Stop: 0 1 6
# Short-Description: start and stop the merlin daemon
# Description: Merlin is an event-distribution system for Nagios
### END INIT INFO


ulimit -c unlimited
prog=merlind
BINDIR=@@BINDIR@@
CONFIG_FILE=@@DESTDIR@@/merlin.conf

start ()
{
	"$BINDIR/$prog" -c "$CONFIG_FILE"
}

stop ()
{
	"$BINDIR/$prog" -c "$CONFIG_FILE" -k
}

case "$1" in
	start)
		start
		;;
	stop)
		stop
		;;
	reload|restart)
		stop
		start
		;;
	*)
		echo "Usage: $0 start|stop|restart|reload"
		exit 1
		;;
esac
