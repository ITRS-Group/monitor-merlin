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

usage: mon sshkey fetch [options] <sources>

Where options can be any combination of:

  --all               Add all configured nodes as sources
  --type=<types>      Make all configured nodes of type sources.
                      <types> can be a comma-separated list containing any
                      combination of peer, poller and master
  --outfile=<outfile> The target file. Defaults to ~/.ssh/authorized_keys

If nodes are configured and either option is either --type or --all,
no sources need to be specified.

END_OF_HELP
}

outfile= destinations=
while test "$#" -ne 0; do
	case "$1" in
	--outfile=*|-o=)
		outfile=$(get_val "$1")
	;;
	--outfile|-o)
		shift
		outfile="$1"
	;;
	--help|-h)
		usage
	;;
	--all)
		more_dest=$(mon node show | sed -n 's/^ADDRESS=//p')
		destinations="$destinations $more_dest"
	;;
	--type=*)
		more_dest=$(mon node show "$1" | sed -n 's/^ADDRESS=//p')
		destinations="$destinations $more_dest"
	;;
	*)
		destinations="$destinations $1"
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

add_key_locally()
{
	dest="$1"
	key=$(grab_key $dest)
	if grep -q "$key" "$outfile"; then
		echo "We already have that key in $outfile"
	else
		echo "Successfully fetched key from $dest to $outfile"
		echo "$key" >> "$outfile"
	fi
}

for dest in $destinations; do
	echo "Fetching public key from $dest"
	case "$dest" in
	*@*)
		add_key_locally "$dest"
	;;
	*)
		add_key_locally "root@$dest"
		add_key_locally "monitor@$dest"
	;;
	esac
done

chmod 600 "$outfile"
