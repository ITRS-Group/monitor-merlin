#!/bin/sh

# some constants
state_file=/etc/op5/distributed/state/log.push.last
nagios_cfg=/opt/monitor/etc/nagios.cfg

if ! test -f $nagios_cfg; then
	echo "$nagios_cfg doesn't exist"
	exit 1
fi

# if the nagios logfile or the archive_dir doesn't exist, we exit
archive_dir=$(sed -n 's/^log_archive_path=\([^ \t]*\).*$/\1/p' $nagios_cfg)
if test -z "$archive_dir"; then
	echo "No log_archive_path specified in $nagios_cfg. Exiting"
	exit 1
fi

if ! test -d "$archive_dir"; then
	echo "$archive_dir doesn't exist, or is not a directory. Exiting"
	exit 1
fi

get_val ()
{
	expr "z$1" : 'z[^=]*=\(.*\)'
}

# get masters and peers. quit early if there are none
targets=$(mon node list --type=master,peer)
test -z "$targets" && exit 0

# we know we have targets. Check if there's something to send
since=
push_all=
for arg in "$@"; do
	case "$1" in
	--since)
		shift
		since="$1"
		;;
	--since=*)
		since=$(get_val "$1")
		;;
	--push-all)
		since=1
		;;
	*)
		echo "Unknown argument: $1"
		exit 1
		;;
	esac
	shift
done

if test -z "$since"; then
	if ! test -f "$state_file"; then
		echo "No 'since' and no last_sent_file. Pushing everything"
		push_all=t
	else
		since=$(cat $state_file)
	fi
fi

# we're really only interested in archived logfiles, so pass the archive
# directory to the import binary
incremental=
test -z "$since" || since="=$since"
files_to_send=$(mon log import --list-files --incremental$since $archive_dir)
test "$files_to_send" || exit 0

send_logs ()
{
	NAME="$1"
	shift
	ADDRESS= TYPE= PUSHED_LOGS_DIR= KEY=
	sshkey=
	echo "Pushing logs to '$NAME'"
	eval $(mon node show $NAME)
	PUSHED_LOGS_DIR="/opt/monitor/pushed_logs/$(hostname)"
	test "$KEY" && sshkey="-i $key"
	test "$ADDRESS" || ADDRESS=$NAME
	cmd="ssh $sshkey $ADDRESS -l monitor mkdir -p $PUSHED_LOGS_DIR"
	$cmd
	cmd="ssh $sshkey $ADDRESS -l monitor touch '$PUSHED_LOGS_DIR/in-transit.log'"
	if ! $cmd; then
		echo "Failed to create in-transit.log muppet file"
		return
	fi
	scp_cmd="scp $sshkey $@ monitor@$ADDRESS:$PUSHED_LOGS_DIR/"
	if ! $scp_cmd; then
		echo "Failed to send logs to monitor@$ADDRESS:$PUSHED_LOGS_DIR/"
		return 1
	else
		if ! ssh $sshkey $ADDRESS -l monitor rm -f "$PUSHED_LOGS_DIR/in-transit.log"; then
			echo "Failed to remove in-transit.log muppet file"
			return 1
		else
			echo "Successfully pushed logs to $ADDRESS:$PUSHED_LOGS_DIR"
			return 0
		fi
	fi
}

# we stash the current time in the last_sent_file. import will list all
# files necessary to push in order to be sure to get all timestamps, but
# since we're only pushing archived files, we can be fairly certain that
# they're complete. Thus, we can avoid pushing an already pushed archive
# by the simple expedient of making sure the last_sent_time is later
# than the last timestamp in that logfile
now=$(date +%s)
fail=
for t in $targets; do
	send_logs $t $files_to_send
	test $? -eq 0 || fail=t
done

test -z "$fail" && echo $now > $state_file
