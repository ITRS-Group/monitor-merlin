#!/bin/bash

src_dir=$(dirname $0)
pushd "$src_dir" >/dev/null 2>&1
src_dir=$(pwd)
popd >/dev/null 2>&1

nagios_cfg=/usr/local/nagios/etc/nagios.cfg
dest_dir=/usr/local/nagios/addons/merlin
root_path=
bindir=/usr/bin
libexecdir=/usr/libexec/merlin
db_type=mysql
db_name=merlin
db_user=merlin
db_pass=merlin
db_root_user=root
db_root_pass=
batch=
install=db,files,config,init,apps

progname="$0"
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
		sed -i "s#^log_file.*#broker_module=$libexecdir/merlin.so $dest_dir/merlin.conf\\n\\n&#" \
			"$nagios_cfg"
		return 0
	fi

	if grep -q "$libexecdir/merlin.so" "$nagios_cfg"; then
		say "merlin.so is already a registered eventbroker in Nagios"
		return 0
	fi

	say "Updating path to merlin.so in $nagios_cfg"
	sed -i "s#broker_module.*merlin.so.*#broker_module=$libexecdir/merlin.so $dest_dir/merlin.conf#" \
		"$nagios_cfg"
	return 0
}

get_arg ()
{
	expr "z$1" : 'z[^=]*=\(.*\)'
}

db_setup ()
{
	mysql="mysql"
	if [ -n "$db_root_user" ]; then
		mysql="$mysql -u$db_root_user"
		test -n "$db_root_pass" && mysql="$mysql -p$db_root_pass"
	fi

	case "$db_type" in
		mysql)
			# Try to connect to database
			$(eval "$mysql -e ''")
			if [ $? -ne 0 ]; then
				echo "Couldn't connect to database, giving up."
				exit 1
			fi
			new_install=0
			# Create database if it do not exist
			db_count=$(eval "$mysql -N -s -e \"SHOW DATABASES LIKE '$db_name'\"" | wc -l)
			if [ $db_count -eq 0 ]; then
				echo "Creating database $db_name"
				eval "$mysql -e \"CREATE DATABASE IF NOT EXISTS $db_name\""
				new_install=1
			fi
			# Always set privileges (to be on the extra safe side)
			eval "$mysql -e \
			  \"GRANT ALL ON $db_name.* TO $db_user@localhost IDENTIFIED BY '$db_pass'\""
			eval "$mysql -e 'FLUSH PRIVILEGES'"

			# Fetch db_version and do upgrade stuff if/when needed
			query="SELECT version FROM db_version"
			db_version=$(eval "$mysql $db_name -BNe \"$query\"" 2>/dev/null)

			# we always run the default schema, since it drops all
			# non-persistent tables. This must come AFTER we fetch
			# $db_version
			eval "$mysql -f $db_name" < $src_dir/sql/mysql/merlin.sql > /tmp/merlin-sql-upgrade.log 2>&1

			# Check for upgrade scripts
			ver=$db_version
			if test "$db_version"; then 
				while true; do
					nextver=$((ver+1))
					f="$src_dir/sql/update-db-${ver}to${nextver}.sql"
					test -f "$f" || break
					eval "$mysql -f $db_name" < $f 2>&1 >>/tmp/merlin-sql-upgrade.log
					ver=$nextver
				done
			fi
			if [ $new_install -eq 1 ]; then
				for index in $src_dir/sql/mysql/*-indexes.sql; do
					eval "$mysql -f $db_name" < $index 2>&1 >>/tmp/merlin-sql-upgrade.log
				done
			else
				# only check for indexes in report_data. sloppy, yes, but should be sufficient
				idx=$(eval "$mysql $db_name -N -s -e \"SHOW INDEX IN report_data\"" | wc -l);
				if [ $idx -eq '1' ]; then
					cat <<EOF
***************
*** WARNING ***
***************

Some of your tables lack indexes, which might cause bad performance.
Installing indexes might take quite some time (several hours for big installations), so I'm going
to let you install them when it's convenient for you.

To install them manually, run:

    mon db fixindexes

EOF
				fi
			fi
			# now we drop the 'id' field from report_data unconditionally
			# It hasn't been used in a long time and causes trouble on
			# very large systems where the 32-bit counter may wrap, causing
			# new entries to the table to be dropped.
			echo "Dropping 'id' column from report_data. This may take a while"
			for table in report_data report_data_extras; do
				eval "$mysql $db_name -e \"ALTER TABLE $table DROP COLUMN id\"" &> /dev/null || :
			done
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
		-e "s#@@LIBEXECDIR@@#$libexecdir#g" \
		-e "s#@@SRCDIR@@#$src_dir#g" -e "s#@@BINDIR@@#$bindir#g" \
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

install_apps ()
{
	mkdir -p $root_path/$bindir
	mkdir -p $root_path/$libexecdir/mon
	macro_subst "$src_dir/apps/mon.py" > "$root_path/$bindir/mon"
	cp -a apps/libexec/* $root_path/$libexecdir/mon
	rm -f $root_path/$libexecdir/mon/-oconf
	cp oconf $root_path/$libexecdir/mon/-oconf
	chmod 755 $root_path/$bindir/mon
	chmod 755 $root_path/$libexecdir/mon/*
}

install_files ()
{
	bins="merlind" # user-visible binaries
	libexecs="import ocimp merlin.so showlog" # binaries that are outside of /opt/monitor lockdown
	files="install-merlin.sh rename" # files going straight to /opt/monitor/op5/merlin/
	subst="example.conf init.sh" # files needing substitution
	execs="import ocimp showlog install-merlin.sh init.sh rename merlind" # everything that should +x
	missing=
	for i in $bins $libexecs $files $execs $subst; do
		if ! test -f "$src_dir/$i"; then
			echo "$src_dir/$i is missing"
			missing="$missing $src_dir/$i"
		fi
	done
	test "$missing" && abort "Essential files are missing. Perhaps you need to run 'make'?"

	test -d "$root_path/$dest_dir" || mkdir -p -m 755 "$root_path/$dest_dir"
	test -d "$root_path/$dest_dir/logs" || mkdir -p -m 777 "$root_path/$dest_dir/logs"
	test -d "$root_path/$dest_dir" || { echo "$root_path/$dest_dir is not a directory"; return 1; }
	test -d "$root_path/$libexecdir" || mkdir -p -m 755 "$root_path/$libexecdir"
	for f in $execs; do
		chmod 755 "$f"
	done

	for f in $files; do
		cp "$src_dir/$f" "$root_path/$dest_dir"
	done
	mkdir -p "$root_path/$dest_dir/sql/mysql/"
	cp -r "$src_dir/sql" "$root_path/$dest_dir"
	macro_subst "$src_dir/example.conf" > "$root_path/$dest_dir/sample.merlin.conf"
	test -f "$root_path/$dest_dir/merlin.conf" || \
		cp "$root_path/$dest_dir/sample.merlin.conf" \
			"$root_path/$dest_dir/merlin.conf"
	macro_subst "$src_dir/import.php" > "$root_path/$dest_dir/import.php"
	macro_subst "$src_dir/object_importer.inc.php" > "$root_path/$dest_dir/object_importer.inc.php"
	macro_subst "$src_dir/MerlinPDO.inc.php" > "$root_path/$dest_dir/MerlinPDO.inc.php"
	macro_subst "$src_dir/oci8topdo.php" > "$root_path/$dest_dir/oci8topdo.php"
	mkdir -p $root_path/$bindir
	for f in $bins; do
		cp "$src_dir/$f" "$root_path/$bindir"
	done
	for f in $libexecs; do
		cp "$src_dir/$f" "$root_path/$libexecdir"
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

show_usage()
{
	cat << END_OF_HELP

usage: $progname [options]

Where options can be any combination of:
  --help|-h                            Print this cruft and exit
  --nagios-cfg=</path/to/nagios.cfg>   Path to nagios.cfg
  --dest-dir=</install/directory>      Where to install Merlin
  --batch                              Assume 'yes' to all questions
  --root=</path/to/fakeroot>           Useful for package builders
  --install=<db,config,files,init>     Components to install. Any combo works
  --db-name=<database name>            Name of database to modify
  --db-type=<mysql>                    Database type. Only mysql for now
  --db-user=<username>                 User merlin should use with db
  --db-pass=<password>                 Password for the db user
  --db-root-user=<db admin>            Database admin username
  --db-root-pass=<pass>                Database admin password
  --bindir=/path/to/binaries           Usually /usr/bin
  --libexecdir=/path/to/libexecdir     Usually /usr/libexec/merlin

END_OF_HELP
	exit 1
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
		--db-root-user=*)
			db_root_user=$(get_arg "$1")
			;;
		--db-root-user)
			db_root_user="$1"
			shift
			;;
		--db-root-pass=*)
			db_root_pass=$(get_arg "$1")
			;;
		--db-root-pass)
			shift
			db_root_pass="$1"
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
		--libexecdir=*)
			libexecdir=$(get_arg "$1")
			;;
		--libexecdir)
			shift
			libexecdir="$1"
			;;
		--bindir=*)
			bindir=$(get_arg "$1")
			;;
		--bindir)
			shift
			bindir="$1"
			;;
		--help|-h)
			show_usage
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
		files|db|config|init|apps) ;;
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
    Root Password (--db-root-pass): $db_root_pass

  Path settings:
    Nagios config file  (--nagios-cfg): $nagios_cfg
    Destination directory (--dest-dir): $dest_dir
    libexecdir          (--libexecdir): $libexecdir
    bindir                  (--bindir): $bindir
    Base root                 (--root): $root_path

  Installing the following components: $install
EOF

case $(ask "Does this look ok? [Y/n]" "ynYN" y) in
	n|N) echo "Aborting installation"; exit 1;;
esac

components=
for i in db config files apps; do
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
if echo "$install" | grep -q 'apps'; then
	install_apps || abort "Failed to install apps"
fi

say 
say "Installation successfully completed"
say
say "You will need to restart Nagios and start Merlind for changes to take effect"
say
