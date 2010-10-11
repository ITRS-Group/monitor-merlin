#!/bin/sh

get_val ()
{
	expr "z$1" : 'z[^=]*=\(.*\)'
}

grab_key() {
	ssh "$1" -C '
dir=$HOME/.ssh
test -d $dir || mkdir -m 700 $dir
for f in id_rsa id_dsa identity; do
	if test -f $dir/$f; then
		keyfile=$dir/$f.pub
		break
	fi
done

if ! test -f "$keyfile"; then
	keyfile=$dir/id_rsa
	ssh-keygen -q -f $keyfile -t rsa -b 2048 -N "" > /dev/null
	keyfile=$keyfile.pub
fi

cat $keyfile
'
}

usage()
{
	cat << END_OF_HELP

usage: mon sshkey fetch [--outfile=<authorized_keys>] <destinations>

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
		destinations="$destinations root@$1 monitor@$1"
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
	if grep -q "$key" "$outfile"; then
		echo "We already have that key in $outfile"
	else
		echo "Successfully fetched key from $dest to $outfile"
		echo "$key" >> "$outfile"
	fi
done

chmod 600 "$outfile"
