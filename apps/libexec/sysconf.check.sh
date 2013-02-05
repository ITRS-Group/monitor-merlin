#!/bin/bash

# bootstrapping

errors=0
verbose=0 # verbose >= 2 means echoing all commands passed to "ok" ("info" commands are always verbose)
# verbose = 0 still shows errors, should really use "exit 1" but the "mon"-wrapper doesn't let us

# @todo support more dists than centos with simple syntax, like
# $centos && $version -eq 5 && ok "my command here"
# for now, you can fallback to use the "info" command (it won't fail)

function usage {
	cat <<CHECK
mon sysconf check
mon sysconf check -v
mon sysconf check -v -v
mon sysconf check --help

A system diagnostics utility. Uses root privileges.

If you wish to use it to debug yourself, run it multiple times with more and more
verbosity applied:
	mon sysconf check
	mon sysconf check -v
	mon sysconf check -v -v

To help op5 help you, please execute this:
	mon sysconf check -v -v > our_op5system.log
and include it with your support errand. Thank you!

OPTIONS
	-v	Verbose mode, pass multiple -v to increase output
CHECK
}

while :; do
	case "$1" in
		-v|--verbose)
			verbose=`expr $verbose + 1`
			;;
		-h|--help|help)
			usage
			exit
			;;
		*)
			break
			;;
	esac
	shift
done

function category {
	test $verbose -ne 0 && echo "=== $1 ==="
}

function ok {
	# call with either:
	# ok "echo yo" "could echo yo"
	# ok ! "xcak" "command xcak should not exist"
	local cmd="$1"
	local invert=0
	if [ "$cmd" = "!" ]; then
		invert=1
		shift
		cmd="$1"
	fi
	local explanation="$2"
	if [ $verbose -gt 1 ]; then
		eval $cmd
	else
		eval $cmd &> /dev/null
	fi
	local status=$?
	if [ $verbose -gt 0 ]; then
		echo -n "$cmd ... "
	fi
	if [ $invert -eq 1 ]; then
		if [ $status -eq 0 ]; then
			status=1
		else
			status=0
		fi
	fi
	if [ $status -ne 0 ]; then
		errors=`expr $errors + 1`
		if [ $verbose -gt 0 ]; then
			echo "FAIL"
			echo -e "\t$explanation"
		fi
	elif [ $verbose -gt 0 ]; then
		echo "OK"
	fi
}

function info {
	# like ok, but ignores errors
	# needs -v -v to output anything
	if [ $verbose -lt 2 ]; then
		return
	fi
	local cmd="$1"
	local explanation="$2"
	echo "$cmd ... INFO"
	test -n "$explanation" && echo "$explanation"
	eval $cmd
}

function show_results {
	test $verbose -ne 0 && echo
	if [ $errors -ne 0 ]; then
		echo "$errors errors found, system is not production ready"
		exit 3
	fi
	echo "System is production ready"
}

function abort_if_not_root {
	if [ $UID -ne 0 ]; then
		echo "You must be root to run this script. Try this:"
		echo "sudo mon sysconf check"
		exit 3
	fi
}

# actual program:

abort_if_not_root

category "USERS"
ok "cat /etc/passwd | grep monitor" "monitor user should exist"

category "FILES, FOLDERS & RIGHTS"
ok "cat /etc/op5-release"
ok "ls -la /etc/op5" "Files should belong to monitor:apache"
info "ls -laR /opt/monitor/etc" "Files should belong to monitor:apache"
info "ls -la /opt/monitor/core*"

category "PROCESSES"
ok "service monitor status" "Try mon start"
ok "service rrdcached status"
ok "service httpd status"
ok "ps -ef | grep synergy"
ok "pidof merlind"
ok "mon node status"

category "SYSTEM"
info "uname -a"
info "head -n 999999 /etc/*release*"
info "date"
info "rpm -qa"
info "ls -la /opt/plugins"
info "head -n 999999 /etc/cron.d/*"
info "hostname"

show_results
