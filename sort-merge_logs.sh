#!/bin/sh
#
# import_logs.sh - poller log concatenation for master servers
#
# This script is meant to be run on a master server when pollers
# have sent in their logfiles. It will execute the op5 import
# program to inject the logfiles into the database, thereby
# making them available for graphing with the pretty, pretty
# report library and gui

pushed_logs=/opt/monitor/pushed_logs
import=/opt/monitor/op5/reports/module/import
log_file=/opt/monitor/var/nagios.log
archive_dir=/opt/monitor/var/archives
test -d $archive_dir &&
	newest_archived=$(/bin/ls --color=none -rt1 $archive_dir/*.log 2>/dev/null | tail -1)

merged=$(mktemp)
trap "rm -f $merged" 0 1 2 3 15
mkdir -m 777 -p $pushed_logs/importing
mv $pushed_logs/*.log.gz $pushed_logs/importing
(cat $newest_archived $log_file; zcat $pushed_logs/importing/*.log.gz) \
	| sort > $merged && \
	$import --incremental $merged && \
	rm -rf $pushed_logs/importing
