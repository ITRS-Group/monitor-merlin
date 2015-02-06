AC_PREREQ([2.59])
AC_INIT([merlin], m4_esyscmd([./generate_version_number.sh]), [dev@op5.org])
AC_CONFIG_SRCDIR([daemon/daemon.c])
AM_INIT_AUTOMAKE([subdir-objects])

AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_LIBTOOL
AM_PATH_PYTHON

PKG_CHECK_MODULES([naemon], [naemon >= 0.8])
naemon_cfg=`$PKG_CONFIG --variable=mainconf naemon`
AS_IF([test "x$naemon_cfg" == "x"],
	  [naemon_cfg=/etc/naemon/naemon.cfg])
AC_SUBST(naemon_cfg)
AC_SYS_LARGEFILE

PKG_CHECK_MODULES([GLIB], [glib-2.0])
PKG_CHECK_MODULES([check], [check])

AC_ARG_ENABLE(auto_postinstall, AS_HELP_STRING([--disable-auto-postinstall], [Don't try to modify naemon's configuration or setup the SQL database. If you disable this, you must manually run the install-merlin.sh script in the source distribution after installation. You should disable this if you build redistributable packages.]))
AM_CONDITIONAL(AUTO_POSTINSTALL, [test "x$enable_auto_postinstall" != "xno"])

AC_ARG_ENABLE(libdbi, AS_HELP_STRING([--disable-libdbi], [Don't use the libdbi database abstraction layer to broker information to mysql]))
AS_IF([test "x$enable_libdbi" != "xno"],
	[
		AC_CHECK_HEADERS([dbi/dbi.h], [], [AC_ERROR([Couldn't find dbi/dbi.h - make sure it's installed and can be found with your CFLAGS, or run configure with --disable-libdbi])])
		AC_CHECK_LIB([dbi], [dbi_initialize], [],[AC_ERROR([Couldn't find the dbi library - make sure it's installed and can be found with your FLAGS, or run configure with --disable-libdbi])] )
		HAVE_LIBDBI=1
		AC_DEFINE(DB_WRAP_CONFIG_ENABLE_LIBDBI, 1, [Set if we build against libdbi])
	])
AM_CONDITIONAL(HAVE_LIBDBI, [test x$HAVE_LIBDBI != x])

AC_ARG_WITH(naemon-config-dir, AS_HELP_STRING([--with-naemon-config-dir], [Install merlin's naemon config into this directory (default is your naemon.cfg directory)]), [naemonconfdir=$withval], [naemonconfdir=`AS_DIRNAME([${naemon_cfg}])`])
AC_SUBST(naemonconfdir)
AC_ARG_WITH(db-type, AS_HELP_STRING([--with-db-type], [Use this database type for logging report data (default=mysql, supported values=mysql)]), [db_type=$withval], [db_type=mysql])
AC_SUBST(db_type)
AC_ARG_WITH(db-name, AS_HELP_STRING([--with-db-name], [Install SQL tables into this database. This database must be created manually. (default=merlin)]), [db_name=$withval], [db_name=merlin])
AC_SUBST(db_name)
AC_ARG_WITH(db-user, AS_HELP_STRING([--with-db-user], [Use this SQL user in merlin during normal operation. This user must be created manually. (default=merlin)]), [db_user=$withval], [db_user=merlin])
AC_SUBST(db_user)
AC_ARG_WITH(db-pass, AS_HELP_STRING([--with-db-pass], [Use this password for merlin's normal database user (default=merlin)]), [db_pass=$withval], [db_pass=merlin])
AC_SUBST(db_pass)

AC_ARG_WITH(naemon-user, AS_HELP_STRING([--with-naemon-user], [The (system) user naemon runs as, which merlin will run as, too (default=autodetect)]), [naemon_user=$withval], [naemon_user=`$PKG_CONFIG --variable=naemon_user naemon`])
AC_SUBST(naemon_user)
AC_ARG_WITH(naemon-group, AS_HELP_STRING([--with-naemon-group], [The (system) group naemon runs as, which merlin will run as, too (default=autodetect)]), [naemon_group=$withval], [naemon_group=`$PKG_CONFIG --variable=naemon_group naemon`])
AC_SUBST(naemon_group)

AC_ARG_WITH(pkgconfdir, AS_HELP_STRING([--with-pkgconfdir], [The directory where merlin installs its configuration (default=${sysconfdir}/merlin)]), [pkgconfdir=$withval], [pkgconfdir=\${sysconfdir}/merlin])
AC_SUBST(pkgconfdir)

binlogdir=${localstatedir}/lib/merlin/binlogs
AC_SUBST(binlogdir)
pkgrundir=${localstatedir}/run/merlin
AC_SUBST(pkgrundir)
AC_ARG_WITH(logdir, AS_HELP_STRING([--with-logdir], [The directory where merlin installs its logs (default=${localstatedir}/log/merlin)]), [logdir=$withval], [logdir=\${localstatedir}/log/merlin])
AC_SUBST(logdir)
cachedir=${localstatedir}/cache/merlin
AC_SUBST(cachedir)

AC_CONFIG_FILES([Makefile data/merlind install-merlin.sh apps/mon.py], [test -f apps/mon.py && cp apps/mon.py apps/op5])
AC_OUTPUT
