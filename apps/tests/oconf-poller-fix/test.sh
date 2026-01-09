#!/bin/sh

self_path=${0%/*}
test $self_path = "test.sh" && self_path=.

mon=$self_path/../../mon.py
libexecdir=${mon%/*}/libexec

# Find Python 3.9 or higher
PYTHON=""
for py in python3.12 python3.11 python3.10 python3.9 python3; do
	if command -v "$py" >/dev/null 2>&1; then
		# Check version is 3.9 or higher
		version=$($py -c "import sys; print('%d.%d' % (sys.version_info.major, sys.version_info.minor))" 2>/dev/null)
		if [ $? -eq 0 ]; then
			major=$(echo "$version" | cut -d. -f1)
			minor=$(echo "$version" | cut -d. -f2)
			if [ "$major" -eq 3 ] && [ "$minor" -ge 9 ]; then
				PYTHON="$py"
				break
			fi
		fi
	fi
done

if [ -z "$PYTHON" ]; then
	echo "Error: Python 3.9 or higher is required but not found" >&2
	exit 1
fi

cwd=$(pwd)
echo "self_path=$self_path"
echo "cwd=$cwd"
cp $self_path/test-config.cfg $self_path/fixme.cfg
# Running this command twice should still result in the same output file
cmd="$PYTHON $mon
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
