#!/bin/sh
#
# Copyright(c) 2008 op5 AB
# License: GPLv2
#

master=
ssh_key=
newest_archived=
last_sent_file=/opt/monitor/op5/log_pusher/last_sent
config_file=/opt/monitor/etc/nagios.cfg
archive_dir=$(sed -n 's/^log_archive_path=\([^ \t]*\).*$/\1/p' $config_file)
log_file=$(sed -n 's/^log_file=\([^ \t]*\).*$/\1/p' $config_file)
test "$log_file" || log_file=/opt/monitor/var/nagios.log
fpart="${log_file##*/}"
test "$archive_dir" || archive_dir=$(echo $log_file | sed 's#/$fpart.*##g')
test -d "$archive_dir" && \
	newest_archived=$(/bin/ls --color=none -rt1 $archive_dir/*.log 2>/dev/null | tail -1)
#echo "fpart: $fpart; log_file: $log_file"
#echo "archive_dir: $archive_dir"
#echo "newest_archived: $newest_archived"
#exit 0

get_lines()
{
	grep --no-filename \
		-e ' HOST ALERT:' -e ' SERVICE ALERT:' \
		-e ' INITIAL HOST STATE:' -e ' INITIAL SERVICE STATE:' \
		-e ' CURRENT HOST STATE:' -e ' CURRENT SERVICE STATE:' \
		-e ' HOST DOWNTIME ALERT:' -e ' SERVICE DOWNTIME ALERT:' \
		"$@" 2>/dev/null | sort
}

tmpfile=$(mktemp)
trap "rm -f $tmpfile" 0 1 2 3 15
test -f "$last_sent_file" && last_sent=$(cat "$last_sent_file" 2>/dev/null)
if [ "$last_sent" ]; then
	get_lines $newest_archived $log_file \
		| sed -n "/^.$last_sent/,\$p" | sed "/^.$last_sent/d" > $tmpfile
else
	last_sent=0
	get_lines "$archive_dir"/* $log_file > $tmpfile
fi

if ! [ -s "$tmpfile" ]; then
	echo "No updates to send."
	exit 0
fi

cur_sent=$(sed -n '$s/^.\([0-9]*\).*/\1/p' $tmpfile)

gzip -9f $tmpfile
trap "rm -f $tmpfile $tmpfile.gz" 0 1 2 3 15
tmpfile="$tmpfile.gz"

dest_name="/opt/monitor/pushed_logs/$(hostname).$last_sent-$cur_sent.log.gz"
	scp -i $ssh_key $tmpfile monitor@$master:$dest_name && \
		echo "$cur_sent" > "$last_sent_file"
