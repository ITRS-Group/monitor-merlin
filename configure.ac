AC_PREREQ([2.59])
AC_INIT([merlin], m4_esyscmd([./generate_version_number.sh]), [dev@op5.org])
AC_CONFIG_SRCDIR([daemon/daemon.c])
AM_INIT_AUTOMAKE([subdir-objects])

AC_CONFIG_HEADERS([config.h])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL
AM_PATH_PYTHON


AM_CFLAGS=$naemon_CFLAGS
AM_CPPFLAGS=-D_GNU_SOURCE
# check for compiler flags. Enabling warnings must come before disabling
AX_CHECK_COMPILE_FLAG([-Wall], AM_CFLAGS+=" -Wall")
AX_CHECK_COMPILE_FLAG([-fPIC], AM_CFLAGS+=" -fPIC")
AX_CHECK_COMPILE_FLAG([-pipe], AM_CFLAGS+=" -pipe")
AX_CHECK_COMPILE_FLAG([-ggdb3], AM_CFLAGS+=" -ggdb3")
#AX_CHECK_COMPILE_FLAG([-Werror], AM_CFLAGS+=" -Werror")
AX_CHECK_COMPILE_FLAG([-Wextra], AM_CFLAGS+=" -Wextra")
AX_CHECK_COMPILE_FLAG([-Wredundant-decls], AM_CFLAGS+=" -Wredundant-decls")
AX_CHECK_COMPILE_FLAG([-Wtrigraphs], AM_CFLAGS+=" -Wtrigraphs")
AX_CHECK_COMPILE_FLAG([-Wdeclaration-after-statement], AM_CFLAGS+=" -Wdeclaration-after-statement")
AX_CHECK_COMPILE_FLAG([-Wshadow], AM_CFLAGS+=" -Wshadow")
AX_CHECK_COMPILE_FLAG([-Wstrict-prototypes], AM_CFLAGS+=" -Wstrict-prototypes")
AX_CHECK_COMPILE_FLAG([-Wno-unused-parameter], AM_CFLAGS+=" -Wno-unused-parameter")
AX_CHECK_COMPILE_FLAG([-Wno-strict-aliasing], AM_CFLAGS+=" -Wno-strict-aliasing")
AX_CHECK_COMPILE_FLAG([-Wno-error=unused-result], AM_CFLAGS+=" -Wno-error=unused-result")
AX_CHECK_COMPILE_FLAG([-lsodium], AM_CFLAGS+=" -lsodium")

AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_CFLAGS])

PKG_CHECK_MODULES([naemon], [naemon >= 0.8])
naemon_cfg=`$PKG_CONFIG --variable=mainconf naemon`
AS_IF([test "x$naemon_cfg" == "x"],
	  [naemon_cfg=/etc/naemon/naemon.cfg])
AC_SUBST(naemon_cfg)
AC_SYS_LARGEFILE

PKG_CHECK_MODULES([GLIB], [glib-2.0])
PKG_CHECK_MODULES([GIO], [gio-2.0])
PKG_CHECK_MODULES([GIO_UNIX], [gio-unix-2.0])
PKG_CHECK_MODULES([check], [check])

# am_missing_prog doesn't seem to fail, so add redundant checks
AM_MISSING_PROG([PYTHON], [python])
AC_CHECK_PROG(PYTHON_CHECK,python,yes)
if test x"$PYTHON_CHECK" != x"yes" ; then
	AC_MSG_ERROR([You need to install python])
fi

AM_MISSING_PROG([GPERF], [gperf])
AC_CHECK_PROG(GPERF_CHECK,gperf,yes)
if test x"$GPERF_CHECK" != x"yes" ; then
	AC_MSG_ERROR([You need to install gperf <https://www.gnu.org/software/gperf>])
fi

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

AC_ARG_WITH(sudodir, AS_HELP_STRING([--with-sudodir], [The directory for sudoers dropdir (default=autodetect)]), [sudodir=$withval], [sudodir=${sysconfdir}/sudoers.d])
AC_SUBST(sudodir)

binlogdir=${localstatedir}/lib/merlin/binlogs
AC_SUBST(binlogdir)
pkgrundir=${localstatedir}/run/merlin
AC_SUBST(pkgrundir)
AC_ARG_WITH(logdir, AS_HELP_STRING([--with-logdir], [The directory where merlin installs its logs (default=${localstatedir}/log/merlin)]), [logdir=$withval], [logdir=\${localstatedir}/log/merlin])
AC_SUBST(logdir)
cachedir=${localstatedir}/cache/merlin
AC_SUBST(cachedir)
AC_ARG_WITH(initdirectory, AS_HELP_STRING([--with-initdirectory], [The directory where merlin installs its initd scripts (default is no directory)]), [initdirectory=$withval], [initdirectory=])
AC_SUBST(initdirectory)
AC_ARG_WITH(initscripts, AS_HELP_STRING([--with-initscripts], [The  merlin init script (default is no script)]), [initscripts=$withval], [initscripts=])
AC_SUBST(initscripts)

AC_CONFIG_FILES([Makefile data/merlind install-merlin.sh])
AC_CONFIG_MACRO_DIR([m4])
AC_OUTPUT
