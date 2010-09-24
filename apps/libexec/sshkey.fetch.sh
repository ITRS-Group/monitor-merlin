#!/bin/sh

get_val ()
{
	expr "z$1" : 'z[^=]*=\(.*\)'
}

grab_key() {
	ssh "$1" -C '
key=$(cat);
dir=.ssh
keyfile=$dir/id_rsa.pub

if ! test -f $keyfile; then
	ssh-keygen -t rsa -b 2048 -P "" > /dev/null
fi
cat $keyfile
'
}

usage()
{
	cat << END_OF_HELP

usage: ssh-grab-keys.sh [--outfile=<authorized_keys>] <destinations>

END_OF_HELP
}

outfile= destinations=
while test "$#" -ne 0; do
	case "$1" in
	--output=*|-o=)
		outfile=$(get_val "$1")
	;;
	--output|-o)
		shift
		outfile="$1"
	;;
	--help|-h)
		usage
	;;
	*@*)
		destinations="$destinations $1"
	;;
	*)
		destinations="$destinations root@$1"
	;;
	esac
	shift
done

if test -z "$outfile"; then
	outfile=~/.ssh/authorized_keys
fi
if ! test -w "$outfile"; then
	mkdir -p -m 700 "$outfile"
	rm -rf $outfile
	chmod 700 $(dirname "$outfile")
fi

test "$destinations" || usage

for dest in $destinations; do
	echo "Fetching public key from $dest"
	key=$(grab_key $dest)
	if grep -q "$key" "$output"; then
		echo "We already have that key in $output"
	else
		echo "Successfully fetched key from $dest to $output"
		echo "$key" >> "$output"
	fi
done

chmod 600 "$outfile"
