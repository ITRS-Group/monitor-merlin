#!/bin/sh

src_dir=$(dirname $0)
pushd "$src_dir" >/dev/null 2>&1
src_dir=$(pwd)
popd >/dev/null 2>&1

nagios_cfg=/opt/monitor/etc/nagios.cfg
dest_dir=/opt/monitor/op5/merlin
root_path=
db_type=mysql
db_name=merlin
db_user=merlin
db_pass=merlin
batch=
install=db,files,config,init

raw_sed_version=$(sed --version | sed '1q')
sed_version=$(echo "$raw_sed_version" | sed -e 's/[^0-9]*//' -e 's/[.]//g')
if [ "$sed_version" -lt 409 ]; then
	echo "You need GNU sed version 4.0.9 or above for this script to work"
	echo "Your sed claims to be \"$raw_sed_version\" ($sed_version)"
	exit 1
fi

abort ()
{
	echo "$@"
	echo "Aborting."
	exit 1
}

modify_nagios_cfg ()
{
	if ! grep -q "merlin.so" "$nagios_cfg"; then
		say "Adding merlin.so as eventbroker to nagios"
		sed -i "s#^log_file.*#broker_module=$dest_dir/merlin.so $dest_dir/merlin.conf\\n\\n&#" \
			"$nagios_cfg"
		return 0
	fi

	if grep -q "$dest_dir/merlin.so" "$nagios_cfg"; then
		say "merlin.so is already a registered eventbroker in Nagios"
		return 0
	fi

	say "Updating path to merlin.so in $nagios_cfg"
	sed -i "s#broker_module.*merlin.so.*#broker_module=$dest_dir/merlin.so $dest_dir/merlin.conf#" \
		"$nagios_cfg"
	return 0
}

get_arg ()
{
	expr "z$1" : 'z[^=]*=\(.*\)'
}

db_setup ()
{
	case "$db_type" in
		mysql)
			# Create database if it do not exist
			if [[ ! $(mysql -e "SHOW DATABASES LIKE '$db_name'") ]]; then
				echo "Creating database $db_name"
				mysql -e "CREATE DATABASE IF NOT EXISTS $db_name"
			fi
			# Always set privileges (to be on the extra safe side)
			mysql -e \
			  "GRANT ALL ON $db_name.* TO $db_user@localhost IDENTIFIED BY '$db_pass'"
			mysql -e 'FLUSH PRIVILEGES'
			# Fetch db_version and do upgrade stuff if/when needed
			query="SELECT version FROM db_version"
			db_version=$(mysql $db_name -BNe "$query" 2>/dev/null)
			case "$db_version" in
				"")
					# No db installed
					mysql $db_name < $src_dir/db.sql
					;;
				"1")
					# DB Version is 1 and db should be re-installed (According to AE)
					mysql $db_name < $src_dir/db.sql
					;;
				*)
					# Unknown version, should we handle this?
					;;
			esac
			;;
		*)
			echo "Unknown database type '$db_type'"
			echo "I understand only lower-case database types."
			return 0
			;;
	esac
}

macro_subst ()
{
	sed -e "s/@@DBNAME@@/$db_name/g" -e "s/@@DBTYPE@@/$db_type/g" \
		-e "s/@@DBUSER@@/$db_user/g" -e "s/@@DBPASS@@/$db_pass/g" \
		-e "s#@@NAGIOSCFG@@#$nagios_cfg#g" -e "s#@@DESTDIR@@#$dest_dir#g" \
		-e "s#@@SRCDIR@@#$src_dir#g" \
		"$@"
}

ask ()
{
	local question options answer
	question="$1" options="$2" default="$3"
	test "$batch" && { echo "$default"; return 0; }

	while true; do
		echo -n "$question " >&2
		read answer
		case "$answer,$default" in
			"",*)
				answer="$default"
				break
				;;
			",") ;;
			*)
				echo "$options " | grep -q "$answer" && break
				;;
		esac
		echo "Please answer one of '$options'" >&2
	done
	echo "$answer" >&1
}

say ()
{
	test "$batch" || echo "$@"
}

install_files ()
{
	missing=
	for i in merlin.so merlind example.conf install-merlin.sh db.sql init.sh; do
		if ! test -f "$src_dir/$i"; then
			echo "$src_dir/$i is missing"
			missing="$missing $src_dir/$i"
		fi
	done
	test "$missing" && abort "Essential files are missing. Perhaps you need to run 'make'?"

	test -d "$root_path/$dest_dir" || mkdir -p 755 "$root_path/$dest_dir"
	test -d "$root_path/$dest_dir/logs" || mkdir -p 777 "$root_path/$dest_dir/logs"
	test -d "$root_path/$dest_dir" || { echo "$root_path/$dest_dir is not a directory"; return 1; }
	for f in merlind merlin.so install-merlin.sh init.sh db.sql example.conf; do
		cp "$src_dir/$f" "$root_path/$dest_dir"
	done
	macro_subst "$src_dir/example.conf" > "$root_path/$dest_dir/merlin.conf"
	macro_subst "$src_dir/import.php" > "$root_path/$dest_dir/import.php"
	macro_subst "$src_dir/object_importer.inc.php" > "$root_path/$dest_dir/object_importer.inc.php"
	for f in merlind import.php install-merlin.sh init.sh; do
		chmod 755 "$root_path/$dest_dir/$f"
	done
	for f in merlin.conf example.conf merlin.so; do
		chmod 644 "$root_path/$dest_dir/$f"
	done
}

install_init ()
{
	if [ $(id -u) -eq 0 -o "$root_path" ]; then
		init_path="$root_path/etc/init.d"
		test -d "$init_path" || mkdir -p "$init_path"
		macro_subst "$src_dir/init.sh" > "$init_path/merlind"
		chmod  755 "$init_path/merlind"
	else
		say "Lacking root permissions, so not installing init-script."
	fi
}


while test "$1"; do
	case "$1" in
		--nagios-cfg=*)
			nagios_cfg=$(get_arg "$1")
			;;
		--nagios-cfg)
			shift
			nagios_cfg="$1"
			;;
		--dest-dir=*)
			dest_dir=$(get_arg "$1")
			;;
		--dest-dir)
			shift
			dest_dir="$1"
			;;
		--db-name=*)
			db_name=$(get_arg "$1")
			;;
		--db-name)
			shift
			db_name="$1"
			;;
		--db-type=*)
			db_type=$(get_arg "$1")
			;;
		--db-type)
			shift
			db_type="$1"
			;;
		--db-user=*)
			db_user=$(get_arg "$1")
			;;
		--db-user)
			shift
			db_user="$1"
			;;
		--db-pass=*)
			db_pass=$(get_arg "$1")
			;;
		--db-pass)
			shift
			db_pass="$1"
			;;
		--batch)
			batch=y
			;;
		--install=*)
			install=$(get_arg "$1")
			;;
		--install)
			shift
			install="$1"
			;;
		--root=*)
			root_path=$(get_arg "$1")
			;;
		--root)
			shift
			root_path="$1"
			;;
		*)
			echo "Illegal argument. I have no idea what to make of '$1'"
			exit 1
			;;
	esac
	shift
done

if [ "$db_pass" = "generate" ]; then
	db_pass=$(dd if=/dev/random bs=32 count=1 | sha1sum | sed -n '$s/\([0-9a-f]*\).*/\1/p')
fi

for c in $(echo "$install" | sed 's/,/ /g'); do
	case "$c" in
		files|db|config|init) ;;
		*)
			echo "I don't know how to install component $c"
			echo "You may only pass one or more of 'db,files,config' to --install"
			echo "and you must pass one of them if you use --install"
			exit 1
			;;
	esac
done

cat << EOF
  Database settings:
    Type     (--db-type): $db_type
    Name     (--db-name): $db_name
    Username (--db-user): $db_user
    Password (--db-pass): $db_pass

  Path settings:
    Nagios config file  (--nagios-cfg): $nagios_cfg
    Destination directory (--dest-dir): $dest_dir
    Base root                 (--root): $root_path

  Installing the following components: $install
EOF

case $(ask "Does this look ok? [Y/n]" "ynYN" y) in
	n|N) echo "Aborting installation"; exit 1;;
esac

components=
for i in db config files; do
	echo "$install" | grep -q $i && components="$i"
done
if ! test "$components"; then
	echo "### No components selected to install."
	echo "### You must pass one or more of 'db', 'config' and 'files'"
	echo "### to the --install argument"
	exit 1
fi

say
say "Installing"
say

if echo "$install" | grep -q 'files'; then
	install_files || abort "Failed to install files."
fi
if echo "$install" | grep -q 'init'; then
	install_init || abort "Failed to install merlind init script"
fi
if echo "$install" | grep -q 'db'; then
	db_setup || abort "Failed to setup database."
fi
if echo "$install" | grep -q 'config'; then
	modify_nagios_cfg || abort "Failed to modify Nagios config."
fi

say 
say "Installation successfully completed"
say
say "You will need to restart Nagios for changes to take effect"
say
