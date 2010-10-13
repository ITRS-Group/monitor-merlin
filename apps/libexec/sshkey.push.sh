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

usage: mon sshkey push [--key=<keyfile.pub>] <destinations>

END_OF_HELP
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

if test -z "$key"; then
	if test -r ~/.ssh/id_rsa.pub; then
		key=~/.ssh/id_rsa.pub
	elif test -r ~/.ssh/id_dsa.pub; then
		key=~/.ssh/id_dsa.pub
	else
		echo "--key not specified and no valid keys found"
		ssh-keygen -t rsa -b 2048 -N "" -f ~/.ssh/id_rsa
		key=~/.ssh/id_rsa.pub
	fi
fi

if ! test -r "$key"; then
	echo "Key '$key' doesn't exist. Generate it first with ssh-keygen"
	exit 1
fi

for dest in $destinations; do
	echo "Appending $key to $dest"
	case "$dest" in
	*@*)
		dest="$dest"
	;;
	*)
		dest="root@$dest"
	;;
	esac
	append_key $key $dest | sed 's/^./  &/'
done
