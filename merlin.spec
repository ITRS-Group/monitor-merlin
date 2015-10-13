%define mod_path /opt/monitor/op5/merlin

%if 0%{?suse_version}
%define mysqld mysql
%define mysql_rpm mysql
%define daemon_group www
%else
%define mysqld mysqld
%define mysql_rpm mysql-server
%define daemon_group apache
%endif

Summary: The merlin daemon is a multiplexing event-transport program
Name: merlin
Version: %{op5version}
Release: %{op5release}%{?dist}
License: GPLv2
Group: op5/Monitor
URL: http://www.op5.se
Source0: %name-%version.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}
Prefix: /opt/monitor
Requires: libaio
Requires: merlin-apps >= %version
Requires: monitor-config
Requires: op5-mysql
Requires: glib2
BuildRequires: mysql-devel
BuildRequires: op5-naemon-devel
BuildRequires: python
BuildRequires: gperf
Obsoletes: monitor-reports-module
BuildRequires: check-devel
BuildRequires: autoconf, automake, libtool
BuildRequires: glib2-devel
BuildRequires: pkgconfig(gio-unix-2.0)

%if 0%{?suse_version}
BuildRequires: libdbi-devel
BuildRequires: pkg-config
Requires: libdbi1
Requires: libdbi-drivers-dbd-mysql
Requires(post): mysql-client
%else
BuildRequires: libdbi-devel
BuildRequires: pkgconfig
Requires: libdbi
Requires: libdbi-dbd-mysql
%endif


%description
The merlin daemon is a multiplexing event-transport program designed to
link multiple Nagios instances together. It can also inject status
data into a variety of databases, using libdbi.


%package -n monitor-merlin
Summary: A Nagios module designed to communicate with the Merlin daemon
Group: op5/Monitor
Requires: op5-naemon, merlin = %version-%release
Requires: monitor-config
Requires: op5-monitor-supported-database

%description -n monitor-merlin
monitor-merlin is an event broker module running inside Nagios. Its
only purpose in life is to send events from the Nagios core to the
merlin daemon, which then takes appropriate action.


%package apps
Summary: Applications used to set up and aid a merlin/ninja installation
Group: op5/Monitor
Requires: rsync
Requires: op5-monitor-supported-database
%if 0%{?suse_version}
Requires: libdbi1
Requires: python-mysql
%else
%if 0%{?rhel} <= 5
Requires: python26
Requires: python26-mysqldb
%else
Requires: MySQL-python
%endif
Requires: libdbi
%endif
Obsoletes: monitor-distributed

%description apps
This package contains standalone applications required by Ninja and
Merlin in order to make them both fully support everything that a
fully fledged op5 Monitor install is supposed to handle.
'mon' works as a single entry point wrapper and general purpose helper
tool for those applications and a wide variety of other different
tasks, such as showing monitor's configuration files, calculating
sha1 checksums and the latest changed such file, as well as
preparing object configuration for pollers, helping with ssh key
management and allround tasks regarding administering a distributed
network monitoring setup.

%package test
Summary: Test files for merlin
Group: op5/Monitor
Requires: monitor-livestatus
Requires: op5-naemon
Requires: merlin merlin-apps monitor-merlin

%description test
Some additional test files for merlin

%prep
%setup -q

%build
echo %{version} > .version_number
autoreconf -i -s
%configure --disable-auto-postinstall --with-pkgconfdir=%mod_path --with-naemon-config-dir=/opt/monitor/etc/mconf --with-naemon-user=monitor --with-naemon-group=%daemon_user --with-logdir=/var/log/op5/merlin

%__make
%__make check

%install
rm -rf %buildroot
%__make install DESTDIR=%buildroot naemon_user=$(id -un) naemon_group=$(id -gn)

ln -s ../../../../usr/bin/merlind %buildroot/%mod_path/merlind
ln -s ../../../../%_libdir/merlin/import %buildroot/%mod_path/import
ln -s ../../../../%_libdir/merlin/ocimp %buildroot/%mod_path/ocimp
ln -s ../../../../%_libdir/merlin/rename %buildroot/%mod_path/rename
ln -s ../../../../%_libdir/merlin/showlog %buildroot/%mod_path/showlog
ln -s ../../../../%_libdir/merlin/merlin.so %buildroot/%mod_path/merlin.so
ln -s op5 %buildroot/%_bindir/mon

# install crontabs
mkdir -p %buildroot%_sysconfdir/cron.d
cp apps/*.cron %buildroot%_sysconfdir/cron.d/

mkdir -p %buildroot/opt/monitor/op5/nacoma/hooks/save/
sed -i 's#@@LIBEXECDIR@@#%_libdir/merlin#' op5build/nacoma_hook.py
install -m 0755 op5build/nacoma_hook.py %buildroot/opt/monitor/op5/nacoma/hooks/save/merlin_hook.py

mkdir -p %buildroot%_sysconfdir/op5kad/conf.d
make data/kad.conf
cp data/kad.conf %buildroot%_sysconfdir/op5kad/conf.d/merlin.kad

mkdir -p %buildroot%_sysconfdir/nrpe.d
cp nrpe-merlin.cfg %buildroot%_sysconfdir/nrpe.d


%post
# we must stop the merlin deamon so it doesn't interfere with any
# database upgrades, logfile imports and whatnot
/etc/init.d/merlind stop >/dev/null || :

# Verify that mysql-server is installed and running before executing sql scripts
service %mysqld status 2&>1 >/dev/null
if [ $? -gt 0 ]; then
  echo "Attempting to start %mysqld..."
  service %mysqld start
  if [ $? -gt 0 ]; then
    echo "Abort: Failed to start %mysqld."
    exit 1
  fi
fi

if ! mysql -umerlin -pmerlin merlin -e 'show tables' > /dev/null 2>&1; then
    mysql -uroot -e "CREATE DATABASE IF NOT EXISTS merlin"
    mysql -uroot -e "GRANT ALL ON merlin.* TO merlin@localhost IDENTIFIED BY 'merlin'"
fi
%_libdir/merlin/install-merlin.sh

/sbin/chkconfig --add merlind || :

# If mysql-server is running _or_ this is an upgrade
# we import logs
if [ $1 -eq 2 ]; then
  mon log import --incremental || :
  mon log import --only-notifications --incremental || :
fi

sed --follow-symlinks -i \
    -e 's#pidfile =.*$#pidfile = /var/run/merlin/merlin.pid;#' \
    -e 's#log_file =.*neb\.log;$#log_file = /var/log/op5/merlin/neb.log;#' \
    -e 's#log_file =.*daemon\.log;$#log_file = /var/log/op5/merlin/daemon.log;#' \
    -e 's#ipc_socket =.*$#ipc_socket = /var/lib/merlin/ipc.sock;#' \
    %mod_path/merlin.conf

# error/warn logging is useless, change it to info
sed --follow-symlinks -r -i \
  's/^([[:space:]]*log_level[[:space:]]*=[[:space:]]*)(error|warn)[[:space:]]*;[[:space:]]*$/\1info;/' \
  %mod_path/merlin.conf

# chown old cached nodesplit data, so it can be deleted
chown -R monitor:%daemon_group %_localstatedir/cache/merlin

# restart all daemons
for daemon in merlind op5kad nrpe; do
	test -f /etc/init.d/$daemon && /etc/init.d/$daemon restart || :
done

%preun -n monitor-merlin
if [ $1 -eq 0 ]; then
	/etc/init.d/merlind stop || :
fi

%postun -n monitor-merlin
if [ $1 -eq 0 ]; then
	# remove the merlin module
	/etc/init.d/monitor restart || :
fi

%post -n monitor-merlin
chown -Rh monitor:%daemon_group %prefix/etc
sed --follow-symlinks -i 's#import_program = php /opt/monitor/op5/merlin/import.php#import_program = /opt/monitor/op5/merlin/ocimp#g' %mod_path/merlin.conf
sed --follow-symlinks -i '/broker_module.*merlin.so.*/d' /opt/monitor/etc/naemon.cfg
/etc/init.d/monitor restart || :


%files
%defattr(-,root,root)
%config(noreplace) %mod_path/merlin.conf
%_datadir/merlin/sql
%mod_path/merlind
%_bindir/merlind
%_libdir/merlin/install-merlin.sh
%_sysconfdir/logrotate.d/merlin
%_sysconfdir/op5kad/conf.d/merlin.kad
%_sysconfdir/nrpe.d/nrpe-merlin.cfg
%_sysconfdir/init.d/merlind
%attr(-, monitor, %daemon_group) %dir %_localstatedir/lib/merlin
%attr(-, monitor, %daemon_group) %dir %_localstatedir/log/op5/merlin
%attr(-, monitor, %daemon_group) %dir %_localstatedir/run/merlin
%attr(-, monitor, %daemon_group) %dir %_localstatedir/cache/merlin
%attr(-, monitor, %daemon_group) %dir %_localstatedir/cache/merlin/config

%files -n monitor-merlin
%defattr(-,root,root)
%_libdir/merlin/merlin.*
%mod_path/merlin.so
%attr(-, monitor, %daemon_group) /opt/monitor/etc/mconf/merlin.cfg
%attr(-, monitor, %daemon_group) %dir %_localstatedir/lib/merlin
%attr(-, monitor, %daemon_group) %dir %_localstatedir/log/op5/merlin

%files apps
%defattr(-,root,root)
%_libdir/merlin/import
%_libdir/merlin/showlog
%_libdir/merlin/ocimp
%_libdir/merlin/rename
%_libdir/merlin/oconf
%mod_path/import
%mod_path/showlog
%mod_path/ocimp
%mod_path/rename
%_libdir/merlin/mon
%_bindir/mon
%_bindir/op5
%_sysconfdir/cron.d/*
/opt/monitor/op5/nacoma/hooks/save/merlin_hook.py*

%attr(600, root, root) %_libdir/merlin/mon/syscheck/db_mysql_check.sh
%attr(600, root, root) %_libdir/merlin/mon/syscheck/fs_ext_state.sh
%attr(600, root, root) %_libdir/merlin/mon/syscheck/proc_smsd.sh

%exclude %_libdir/merlin/mon/test.py*
%exclude %_libdir/merlin/merlin.*

%files test
%defattr(-,root,root)
%_libdir/merlin/mon/test.py*

%clean
rm -rf %buildroot

%changelog
* Tue Mar 17 2009 Andreas Ericsson <ae@op5.se>
- Initial specfile creation.
