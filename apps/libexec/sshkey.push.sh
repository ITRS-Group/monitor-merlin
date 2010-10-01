#!/bin/sh

get_val ()
{
	expr "z$1" : 'z[^=]*=\(.*\)'
}

append_key() {
	cat $1 | ssh "$2" -C '
key=$(cat);
dir=.ssh
keyfile=$dir/authorized_keys

if ! test -d $dir; then
	mkdir $dir
	chmod 700 .ssh
fi

if ! test -f .ssh/authorized_keys; then
	echo "$key" > $keyfile
	chmod 600 $keyfile
	echo "Keyfile $keyfile created successfully"
elif grep -q "$key" $keyfile; then
	echo "Key already installed";
else
	echo "$key" >> ~/.ssh/authorized_keys;
	echo "Successfully installed public ssh key";
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
	*@*)
		destinations="$destinations $1"
	;;
	*)
		destinations="$destinations root@$1 monitor@$1"
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
		ssh-keygen -t rsa -b 2048
	fi
else
	if ! test -r "$key"; then
		echo "Key '$key' doesn't exist. Generate it first with ssh-keygen"
		exit 1
	fi
fi
test -f "$key" || exit 0

for dest in $destinations; do
	echo "Appending $key to $dest"
	append_key $key $dest | sed 's/^./  &/'
done
