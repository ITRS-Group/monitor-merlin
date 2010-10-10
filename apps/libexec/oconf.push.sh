#!/bin/sh
# we sort of know that we only get run when configuration needs
# to be pushed somewhere, which usually means it has to be pushed
# everywhere.
#
# For this first edition of the oconf.push script, we simply ignore
# everything called "arguments" and just focus on getting a working
# configuration out to the various pollers and peers we have
# configured.
#
# The mon helper is f*cking awesome for this, as it lists all the
# pollers and peers from the merlin configuration
#
# Note that ssh-keys need to be installed on all systems

# where we'll cache the split-out configuration
cache_dir=/var/cache/merlin
conf_dir=$cache_dir/config
lock_file=$cache_dir/.push.lock
test -d $cache_dir || mkdir -p $cache_dir
test -d $conf_dir || mkdir -p $conf_dir

while test "$#" -gt 0; do
	case "$1" in
	--force)
		rm -f $lock_file
		;;
	esac
	shift
done

# there's room for a small race here, but the only thing it
# should lead to is a few wasted cycles so we don't worry too
# much about that. We do take care of an overly stale lockfile
# though. 5 minutes should be about enough.
now=$(date +%s)
if test -f $lock_file; then
	locked=$(cat $lock_file)
	expires=$(expr $now - $locked - 300)
	if test $expires -lt 0; then
		when=$(echo $expires | cut -b2-)
		echo "push already in progress. Lock expires in $when seconds. Aborting"
		exit 0
	fi
	echo "Stale lockfile expired $expires seconds ago"
fi
echo $now > $lock_file
# make sure the lockfile is removed when we exit
trap 'rm -f $lock_file' EXIT 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15

pollers=$(mon node list --type=poller)
peers=$(mon node list --type=peer)

# possibly no peers or pollers. In that case we really shouldn't be
# run at all, but if we are we exit immediately
if test -z "$pollers" -a -z "$peers"; then
	exit 0
fi

if test "$pollers"; then
	rm -rf $conf_dir
	mkdir -m 700 $conf_dir
	split_args=
	for node in $pollers; do
		HOSTGROUP=
		conf=$(mon node show $node)
		HOSTGROUP=$(echo "$conf" | sed -n 's/^HOSTGROUP=//p')
		if test -z "$HOSTGROUP"; then
			echo "Poller $node has no hostgroups assigned to it."
			echo "Merlin doesn't accept pollers without hostgroups."
			echo "Please fix your configuration."
			exit 1
		fi
		split_args="$split_args '$conf_dir/$node:$HOSTGROUP'"
	done
	cmd="mon oconf split $split_args"
#	echo "Running: $cmd"
	$cmd
fi

send_to_node()
{
	node="$1"
	ADDRESS=
	OCONF_PATH=
	OCONF_SSH_KEY=
	SSH_ARGS=
	OCONF_DEST=
	foo=$(mon node show $node)
	test $? -ne 0 && continue
	eval $foo
	test -z "$OCONF_PATH" && OCONF_PATH=/opt/monitor/etc
	test -z "$ADDRESS" && ADDRESS=$node
	test "$OCONF_SSH_KEY" && SSH_ARGS="-i $OCONF_SSH_KEY"
	test "$OCONF_SSH_USER" || OCONF_SSH_USER=monitor
	echo "Sending to $TYPE node $node"
	if [ "$TYPE" = 'poller' ]; then
		src=$conf_dir/$node
		test -z "$OCONF_DEST" && OCONF_DEST=/opt/monitor/etc/from-master.cfg
	else
		src=$OCONF_PATH
		test "$OCONF_DEST" || OCONF_DEST=$src
	fi
	cmd="scp -C -p $SSH_ARGS -r $src $OCONF_SSH_USER@$ADDRESS:$OCONF_DEST"
#	echo "Running: $cmd"
	$cmd
	cmd="ssh $SSH_ARGS root@$ADDRESS /etc/init.d/monitor restart"
#	echo "Running: $cmd"
	$cmd
}

for node in $peers $pollers; do
	send_to_node $node
done
