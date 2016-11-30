#!/bin/sh

self_path=${0%/*}
test $self_path = "test.sh" && self_path=.

mon=$self_path/../../mon.py
libexecdir=${mon%/*}/libexec

cwd=$(pwd)
echo "self_path=$self_path"
echo "cwd=$cwd"
cp $self_path/test-config.cfg $self_path/fixme.cfg
# Running this command twice should still result in the same output file
cmd="python $mon
	--libexecdir=$libexecdir
	--merlin-cfg=$self_path/merlin.conf
	--nagios-conf=$self_path/fixme.cfg
	oconf poller-fix $self_path/fixme.cfg"

$cmd || exit 1
diff -u $self_path/fixme.cfg $self_path/fixed-config.cfg
result1=$?

cp $self_path/fixed-config.cfg $self_path/fixed-config.cfg-first
$cmd || exit 1
diff -u $self_path/fixed-config.cfg-first $self_path/fixed-config.cfg
result2=$?

if test $result1 -ne 0; then
	echo "fail: Files differ" >&2
	exit 1
fi
if test $result2 -ne 0; then
	echo "fail: Files differ after second pass" >&2
	exit 1
fi
