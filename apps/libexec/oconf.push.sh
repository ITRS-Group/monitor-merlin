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
cache_dir=/var/cache/merlin/config

pollers=$(mon node list --type=poller)
peers=$(mon node list --type=peer)

# possibly no peers or pollers. In that case we really shouldn't be
# run at all, but if we are we exit immediately
if test -z "$pollers" -a -z "$peers"; then
	exit 0
fi

if test "$pollers"; then
	rm -rf $cache_dir
	mkdir -m 700 $cache_dir
	split_args=
	for node in $pollers; do
		HOSTGROUP=
		ADDRESS=
		eval $(mon node show $node)
		split_args="$split_args $cache_dir/$node:$HOSTGROUP"
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
		src=$cache_dir/$node
		test -z "$OCONF_DEST" && OCONF_DEST=/opt/monitor/etc/from-master.cfg
	else
		src=$OCONF_PATH
		OCONF_DEST="/tmp/csync"
	fi
	cmd="scp -C -p $SSH_ARGS -r $src $OCONF_SSH_USER@$ADDRESS:$OCONF_DEST"
#	echo "Running: $cmd"
	$cmd
}

for node in $peers $pollers; do
	send_to_node $node
done
