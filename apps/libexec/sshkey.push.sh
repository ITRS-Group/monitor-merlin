#!/bin/sh

get_val ()
{
	expr "z$1" : 'z[^=]*=\(.*\)'
}

append_key() {
	cat $1 | ssh "$2" -C '
key=$(cat);
uid=$(id -u)

install_key()
{
	keyfile="$1"
	keydir=$(dirname "$keyfile")
	if ! test -d $keydir; then
		echo "Creating $keydir"
		mkdir -p -m 700 $keydir
	fi

	if ! test -f $keyfile; then
		echo "$key" > $keyfile
		echo "Keyfile $keyfile created successfully"
	elif grep -q "$key" $keyfile; then
		echo "Key already installed in $keyfile";
	else
		echo "$key" >> $keyfile
		echo "Successfully installed public ssh key";
	fi
	chmod 700 "$keydir"
	chmod 600 "$keydir/"*
}

install_key ~/.ssh/authorized_keys
# If we logged in as root we set up the key for monitor as well
if test "$uid" -eq 0; then
	install_key ~monitor/.ssh/authorized_keys
	chown -R monitor ~monitor/.ssh
fi
'
}

usage()
{
	cat << END_OF_HELP
[--key=<keyfile>] [--all|--type=<peer|poller|master>] [destination]..
Pushes public SSH key to specified remote node(s) (destinations).

The key will be read from specified keyfile, or if not
specified, default to: ~/.ssh/id_rsa.pub

If no key exists, a new key will be generated automatically.

A combination of different node types as well as specific nodes
can be specified at the same time.

END_OF_HELP

	exit # since the script wont need to do anything more
}

key= destinations=
while test "$#" -ne 0; do
	case "$1" in
	--key=*|-k=)
		key=$(get_val "$1")
	;;
	--key|-k)
		shift
		key="$1"
	;;
	--help|-h)
		usage
	;;
	--all)
		more_dests=$(mon node show | sed -n 's/^ADDRESS=//p')
		destinations="$destinations $more_dests"
	;;
	--type=*)
		more_dests=$(mon node show "$1" | sed -n 's/^ADDRESS=//p')
		destinations="$destinations $more_dests"
	;;
	*)
		destinations="$destinations $1"
	;;
	esac
	shift
done

find_key_for_user()
{
	if test -z "$key"; then
		if test -r ~/.ssh/id_rsa.pub; then
			key=~/.ssh/id_rsa.pub
		elif test -r ~/.ssh/id_dsa.pub; then
			key=~/.ssh/id_dsa.pub
		else
			echo "--key not specified and no valid keys found - generating new one" >&2
			ssh-keygen -q -t rsa -b 2048 -N "" -f ~/.ssh/id_rsa
			key=~/.ssh/id_rsa.pub
		fi
	fi
	echo "$key"
}

for dest in $destinations; do
	echo "Appending $key to $dest"
	case "$dest" in
	*@*)
		append_key $(find_key_for_user) $dest | sed 's/^./  &/'
	;;
	*)
		# first install our key into (root and) monitor user
		append_key $(find_key_for_user) $dest | sed 's/^./  &/'
		# and then use that key for uploading monitor's key to monitor user
		if [ -z "$key" -a $(whoami) == "root" ]; then
			export -f find_key_for_user
			append_key $(su monitor -c find_key_for_user) "monitor@$dest" | sed 's/^./  &/'
		fi
	;;
	esac
done
