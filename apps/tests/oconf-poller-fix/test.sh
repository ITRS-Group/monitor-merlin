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
#echo $cmd && exit 0
$cmd || exit 1
cmp --quiet $self_path/fixme.cfg $self_path/fixed-config.cfg
result1=$?
$cmd || exit 1
result2=$?
#rm -f $self_path/fixme.cfg

if test $result1 -ne 0; then
	echo "fail: Files differ"
	exit 1
fi
if test $result2 -ne 0; then
	echo "fail: Files differ after second pass"
fi

echo "pass: Files match"
exit 0
