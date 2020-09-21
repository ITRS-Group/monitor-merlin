AM_CFLAGS += $(CFLAGS) -I$(srcdir)/shared $(naemon_CFLAGS)
AM_CPPFLAGS += $(CPPFLAGS) -DBINLOGDIR='"$(binlogdir)"' -DPKGRUNDIR='"$(pkgrundir)"' -DLOGDIR='"$(logdir)"' -DCACHEDIR='"$(cachedir)"'
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = merlind

merlinlibdir = $(pkglibdir)
merlinlib_PROGRAMS = import showlog rename oconf keygen
merlinlib_SCRIPTS = install-merlin.sh
bin_SCRIPTS = apps/op5

mondir = $(libdir)/merlin/mon
mon_SCRIPTS = \
	apps/libexec/ascii.py \
	apps/libexec/check.py \
	apps/libexec/db.py \
	apps/libexec/ecmd.py \
	apps/libexec/log.py \
	apps/libexec/merlinkey.py \
	apps/libexec/node.py \
	apps/libexec/node.tree.php \
	apps/libexec/oconf.py \
	apps/libexec/qh.py \
	apps/libexec/query.ls.sh \
	apps/libexec/query.lsq.sh \
	apps/libexec/reportdata.py \
	apps/libexec/restart.sh \
	apps/libexec/sshkey.fetch.sh \
	apps/libexec/sshkey.push.sh \
	apps/libexec/start.sh \
	apps/libexec/status.py \
	apps/libexec/stop.sh \
	apps/libexec/sysconf.check.sh \
	apps/libexec/sysconf.ramdisk.sh \
	apps/libexec/test.py

monmoduledir = $(mondir)/modules
monmodule_PYTHON = apps/libexec/modules/*.py
# mai gawd
monmodulepytapdir = $(monmoduledir)/pytap
monmodulepytap_PYTHON = apps/libexec/modules/pytap/*.py
monsyscheckdir = $(mondir)/syscheck
monsyscheck_SCRIPTS = \
	apps/libexec/syscheck/db_mysql_check.sh \
	apps/libexec/syscheck/fs_ext_state.sh \
	apps/libexec/syscheck/fs_free_inodes.sh \
	apps/libexec/syscheck/fs_free_space.sh \
	apps/libexec/syscheck/fs_mount_opts.sh \
	apps/libexec/syscheck/license.sh \
	apps/libexec/syscheck/proc_collector.sh \
	apps/libexec/syscheck/proc_crond.sh \
	apps/libexec/syscheck/proc_httpd.sh \
	apps/libexec/syscheck/proc_merlind.sh \
	apps/libexec/syscheck/proc_monitor_core.sh \
	apps/libexec/syscheck/proc_monitor_worker.sh \
	apps/libexec/syscheck/proc_mysqld.sh \
	apps/libexec/syscheck/proc_npcd.sh \
	apps/libexec/syscheck/proc_ntpd.sh \
	apps/libexec/syscheck/proc_op5config.sh \
	apps/libexec/syscheck/proc_op5kad.sh \
	apps/libexec/syscheck/proc_pgsql_postmaster.sh \
	apps/libexec/syscheck/proc_processor.sh \
	apps/libexec/syscheck/proc_rrdcached.sh \
	apps/libexec/syscheck/proc_smsd.sh \
	apps/libexec/syscheck/proc_synergy.sh \
	apps/libexec/syscheck/proc_syslog_ng.sh

monhelpfilesdir = $(mondir)/helpfiles
monhelpfiles_DATA = apps/libexec/helpfiles/*
monbashdir = $(mondir)/bash
monbash_SCRIPTS = apps/libexec/bash/*

pkglib_LTLIBRARIES = merlin.la

pkgconf_DATA = data/merlin.conf
sudo_DATA = sudo/merlin

sqldir = $(datarootdir)/merlin
nobase_sql_DATA = sql/update-db-1to2.sql sql/update-db-2to3.sql sql/mysql/*

naemonconf_DATA = data/merlin.cfg

merlin_la_LDFLAGS = -module -shared -fPIC
merlin_la_LIBADD = $(GLIB_LIBS)
merlin_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS) -DMERLIN_MODULE_BUILD
merlin_la_CPPFLAGS = $(AM_CPPFLAGS)
merlind_LDADD = $(naemon_LIBS) $(AM_LDADD) $(GLIB_LIBS)
merlind_CPPFLAGS = $(AM_CPPFLAGS)
merlind_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS) -DMERLIN_DAEMON_BUILD

initdir = $(initdirectory)
init_SCRIPTS = $(initscripts)
EXTRA_SCRIPTS = data/merlind

logrotatedir = $(sysconfdir)/logrotate.d
logrotate_DATA = data/merlin

common_sources = \
	shared/logging.c shared/logging.h \
	shared/cfgfile.c shared/cfgfile.h \
	shared/shared.c shared/shared.h \
	shared/dlist.c shared/dlist.h \
	shared/compat.h
shared_sources = $(common_sources) \
	shared/ipc.c shared/ipc.h \
	shared/io.c shared/io.h \
	shared/encryption.c shared/encryption.h \
	shared/node.c shared/node.h \
	shared/codec.c shared/codec.h \
	shared/binlog.c shared/binlog.h \
	shared/configuration.c shared/configuration.h

db_wrap_sources = daemon/sql.c daemon/sql.h daemon/db_wrap.c daemon/db_wrap.h
if HAVE_LIBDBI
db_wrap_sources += daemon/db_wrap_dbi.c daemon/db_wrap_dbi.h
else
endif

module_sources = $(shared_sources) \
	module/module.c module/module.h \
	module/hooks.c module/hooks.h \
	module/misc.c module/misc.h \
	module/sha1.c module/sha1.h \
	module/queries.c module/queries.h \
	module/script-helpers.c module/script-helpers.h \
	module/oconfsplit.c module/oconfsplit.h \
	module/net.c module/net.h \
	shared/pgroup.c shared/pgroup.h \
	module/testif_qh.c module/testif_qh.h
daemon_sources = $(shared_sources) $(db_wrap_sources) \
	daemon/daemonize.c daemon/daemonize.h \
	daemon/db_updater.c daemon/db_updater.h \
	daemon/state.c daemon/state.h \
	daemon/string_utils.c daemon/string_utils.h
app_sources = $(common_sources) \
	daemon/state.c daemon/state.h \
	tools/logutils.c tools/logutils.h \
	tools/lparse.c tools/lparse.h \
	tools/test_utils.c tools/test_utils.h \
	tools/colors.h

merlind_SOURCES = $(daemon_sources) \
	daemon/daemon.c daemon/daemon.h \
	daemon/merlind.c
merlin_la_SOURCES = $(module_sources)
showlog_SOURCES = $(app_sources) \
	tools/showlog.c \
	tools/auth.c tools/auth.h
showlog_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/daemon $(GLIB_CFLAGS)
showlog_LDADD = $(naemon_LIBS) $(AM_LDADD) $(GLIB_LIBS)
import_SOURCES = $(app_sources) tools/import.c $(db_wrap_sources)
import_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/daemon $(GLIB_CFLAGS)
import_LDADD = $(naemon_LIBS) $(AM_LDADD) $(GLIB_LIBS)
oconf_SOURCES = tools/oconf.c module/sha1.c module/misc.c shared/shared.c shared/shared.h shared/logging.c shared/logging.h
oconf_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/module $(GLIB_CFLAGS)
oconf_LDADD = $(naemon_LIBS) $(AM_LDADD) $(GLIB_LIBS)

rename_SOURCES = $(app_sources) tools/rename.c $(db_wrap_sources)
rename_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/daemon $(GLIB_CFLAGS)
rename_LDADD = $(naemon_LIBS) $(AM_LDADD) $(GLIB_LIBS)

keygen_SOURCES = tools/keygen.c
keygen_CFLAGS = $(AM_CFLAGS)
keygen_LDADD = -lsodium

check_PROGRAMS = $(TESTS) test-dbwrap merlincat cukemerlin
TESTS = sltest test-csync test-lparse hooktest stringutilstest showlogtest bltest codectest importlogtest
TESTS_ENVIRONMENT = G_DEBUG=fatal-criticals; export G_DEBUG;

sltest_SOURCES = tests/sltest.c tools/test_utils.c tools/slist.c tools/slist.h
sltest_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/tools
test_csync_SOURCES = tests/test-csync.c tools/test_utils.c $(module_sources)
test_csync_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/module -I$(srcdir)/tools $(GLIB_CFLAGS)
test_csync_LDADD = $(naemon_LIBS)
test_lparse_SOURCES = tests/test-lparse.c tools/lparse.c tools/logutils.c tools/test_utils.c
test_lparse_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/tools $(GLIB_CFLAGS)
test_lparse_CPPFLAGS = $(AM_CPPFLAGS)
test_lparse_LDADD = $(naemon_LIBS)
hooktest_SOURCES = tests/test-hooks.c module/net.c module/testif_qh.c module/script-helpers.c shared/cfgfile.c shared/shared.c shared/logging.c shared/dlist.c shared/io.c shared/encryption.c shared/node.c shared/codec.c shared/binlog.c module/misc.c module/sha1.c tools/test_utils.c module/oconfsplit.c shared/configuration.c module/queries.c
hooktest_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/module -I$(srcdir)/tools $(check_CFLAGS) $(GLIB_CFLAGS)
hooktest_LDADD = $(naemon_LIBS) $(check_LIBS)
stringutilstest_SOURCES = tests/test-stringutils.c tools/test_utils.c daemon/string_utils.c
stringutilstest_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/tools -I$(srcdir)/daemon
showlogtest_SOURCES = tests/test-showlog.c $(app_sources)
showlogtest_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/tools -I$(srcdir)/daemon $(check_CFLAGS) $(GLIB_CFLAGS)
showlogtest_LDADD = $(naemon_LIBS) $(check_LIBS)
bltest_SOURCES = tests/bltest.c shared/binlog.c tools/test_utils.c
bltest_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/tools
codectest_SOURCES = tests/codectest.c shared/codec.c shared/logging.h shared/shared.c
codectest_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/shared $(check_CFLAGS)
codectest_LDADD = $(naemon_LIBS) $(check_LIBS)
importlogtest_SOURCES = tests/test-importlog.c $(app_sources)
importlogtest_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/tools -I$(srcdir)/daemon $(check_CFLAGS) $(GLIB_CFLAGS)
importlogtest_LDADD = $(naemon_LIBS) $(check_LIBS)

# So, it turns out that this test isn't actually a test that has ever executed
# It's just a binary that could be used for writing tests against. I give up :(
test_dbwrap_SOURCES = tests/test-dbwrap.c $(shared_sources) $(db_wrap_sources)
test_dbwrap_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/daemon
test_dbwrap_LDADD = $(naemon_LIBS) $(AM_LDADD)


test-apps: apps/libexec/oconf.py
	@for t in apps/tests/*; do sh $$t/test.sh; done

check: test-apps

install-exec-hook:
if AUTO_POSTINSTALL
	bash install-merlin.sh
else
endif
	$(mkinstalldirs) -o $(naemon_user):$(naemon_group) $(DESTDIR)$(binlogdir)
	$(mkinstalldirs) -o $(naemon_user):$(naemon_group) $(DESTDIR)$(logdir)
	$(mkinstalldirs) -o $(naemon_user):$(naemon_group) $(DESTDIR)$(pkgrundir)
	$(mkinstalldirs) -o $(naemon_user):$(naemon_group) $(DESTDIR)$(cachedir)
	$(mkinstalldirs) -o $(naemon_user):$(naemon_group) $(DESTDIR)$(cachedir)/config

data/merlin data/merlin.cfg data/merlin.conf data/kad.conf sudo/merlin apps/libexec/syscheck/proc_merlind.sh apps/libexec/oconf.py apps/mon.py apps/libexec/restart.sh apps/libexec/merlinkey.py: % : %.in
	$(AM_V_GEN) sed \
		-e 's,@PYTHON[@],$(PYTHON),g' \
		-e 's,@pkgconfdir[@],$(pkgconfdir),g' \
		-e 's,@naemon_cfg[@],$(naemon_cfg),g' \
		-e 's,@prefix[@],$(prefix),g' \
		-e 's,@exec_prefix[@],$(exec_prefix),g' \
		-e 's,@localstatedir[@],$(localstatedir),g' \
		-e 's,@libdir[@],$(libdir),g' \
		-e 's,@logdir[@],$(logdir),g' \
		-e 's,@naemon_user[@],$(naemon_user),g' \
		-e 's,@pkgrundir[@],$(pkgrundir),g' \
		-e 's,@cachedir[@],$(cachedir),g' \
		-e 's,@merlin_cfgdir[@],$(pkgconfdir),g' \
		-e 's,@db_name[@],$(db_name),g' \
		-e 's,@db_type[@],$(db_type),g' \
		-e 's,@db_user[@],$(db_user),g' \
		-e 's,@db_pass[@],$(db_pass),g' \
		$< > $@

GENERATED_NEBEV = \
	tests/merlincat/nebev2kvvec.c tests/merlincat/nebev2kvvec.h \
	tests/merlincat/kvvec2nebev.c tests/merlincat/kvvec2nebev.h \
	tests/merlincat/nebev-col2key.gperf

$(GENERATED_NEBEV): tests/merlincat/generate-nebev2kvvec.py
	$(AM_V_GEN) python tests/merlincat/generate-nebev2kvvec.py

tests/merlincat/nebev-col2key.c: tests/merlincat/nebev-col2key.gperf
	$(AM_V_GEN) $(GPERF) --switch=1 --struct-type \
			--multiple-iterations=10 --readonly-tables \
			--hash-function-name=nebev_col_phash \
			--lookup-function-name=nebev_col_key \
			--language=ANSI-C \
		$< > $@

tests/merlincat/kvvec2nebev.c: tests/merlincat/nebev-col2key.c

BUILT_SOURCES = $(GENERATED_NEBEV) tests/merlincat/nebev-col2key.c \
	 apps/libexec/restart.sh apps/libexec/oconf.py apps/mon.py
CLEANFILES = $(BUILT_SOURCES)

merlincat_SOURCES = tests/merlincat/merlincat.c \
	tests/merlincat/client_gsource.c tests/merlincat/client_gsource.h \
	tests/merlincat/server_gsource.c tests/merlincat/server_gsource.h \
	tests/merlincat/merlinreader.c tests/merlincat/merlinreader.h \
	tests/merlincat/merlincat_codec.c tests/merlincat/merlincat_codec.h \
	tests/merlincat/console.c tests/merlincat/console.h \
	tests/merlincat/event_packer.c tests/merlincat/event_packer.h \
	tests/merlincat/merlincat_encryption.c tests/merlincat/merlincat_encryption.h \
	tests/merlincat/conn_info.c tests/merlincat/conn_info.h \
	tests/merlincat/nebev2kvvec.c tests/merlincat/nebev2kvvec.h \
	tests/merlincat/kvvec2nebev.c tests/merlincat/kvvec2nebev.h

merlincat_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(GIO_UNIX_CFLAGS)
merlincat_LDADD = $(naemon_LIBS) $(GLIB_LIBS) $(GIO_LIBS) $(GIO_UNIX_LIBS) -lsodium

cukemerlin_SOURCES = tests/cukemerlin/base/cukemerlin.c \
	tests/cukemerlin/base/json.c tests/cukemerlin/base/json.h \
	tests/cukemerlin/base/jsonx.c tests/cukemerlin/base/jsonx.h \
	tests/cukemerlin/base/cukesocket.c tests/cukemerlin/base/cukesocket.h \
	tests/cukemerlin/base/jsonsocket.c tests/cukemerlin/base/jsonsocket.h \
	tests/cukemerlin/steps/steps.c tests/cukemerlin/steps/steps.h \
	tests/cukemerlin/steps/steps_test.c tests/cukemerlin/steps/steps_test.h \
	tests/cukemerlin/steps/steps_merlin.c tests/cukemerlin/steps/steps_merlin.h \
	tests/cukemerlin/steps/steps_daemons.c tests/cukemerlin/steps/steps_daemons.h \
	tests/cukemerlin/steps/steps_config.c tests/cukemerlin/steps/steps_config.h \
	tests/cukemerlin/steps/steps_queryhandler.c tests/cukemerlin/steps/steps_queryhandler.h \
	tests/cukemerlin/steps/steps_livestatus.c tests/cukemerlin/steps/steps_livestatus.h \
	\
	tests/merlincat/client_gsource.c tests/merlincat/client_gsource.h \
	tests/merlincat/server_gsource.c tests/merlincat/server_gsource.h \
	tests/merlincat/merlinreader.c tests/merlincat/merlinreader.h \
	tests/merlincat/merlincat_codec.c tests/merlincat/merlincat_codec.h \
	tests/merlincat/console.c tests/merlincat/console.h \
	tests/merlincat/event_packer.c tests/merlincat/event_packer.h \
	tests/merlincat/merlincat_encryption.c tests/merlincat/merlincat_encryption.h \
	tests/merlincat/conn_info.c tests/merlincat/conn_info.h \
	tests/merlincat/nebev2kvvec.c tests/merlincat/nebev2kvvec.h \
	tests/merlincat/kvvec2nebev.c tests/merlincat/kvvec2nebev.h

cukemerlin_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(GIO_UNIX_CFLAGS) -I$(top_builddir)/tests/cukemerlin -I$(top_builddir)/tests
cukemerlin_LDADD = $(naemon_LIBS) $(GLIB_LIBS) $(GIO_LIBS) $(GIO_UNIX_LIBS) -lsodium


apps/op5: apps/mon.py
	cp $< $@

dist-hook:
	@if test -d "$(srcdir)/.git"; \
	then \
		echo Creating ChangeLog && \
		( cd "$(top_srcdir)" && \
		  echo '# Generated by Makefile. Do not edit.'; echo; \
		  $(top_srcdir)/missing --run git log --pretty=format:"%ai %an <%ae>%n%n	%s%n" ) > ChangeLog.tmp \
		&& mv -f ChangeLog.tmp $(top_distdir)/ChangeLog \
		|| ( rm -f ChangeLog.tmp ; \
		echo Failed to generate ChangeLog >&2 ); \
		echo Creating AUTHORS && \
		( cd "$(top_srcdir)" && \
			echo "The following people have contributed to Merlin with patches,"; \
			echo "bugreports, testing or ideas in one way or another."; \
			echo "Our sincerest thanks to all of you."; echo; \
			$(top_srcdir)/missing --run git log | sed -n 's/.*-.*:[\t ]\(.*\)[\t ]\+<.*@.*>.*/  \1/p' | sort -u; echo; \
			echo "To view authors sorted by contributions, use 'git shortlog -ns'";) > AUTHORS.tmp \
		&& mv -f AUTHORS.tmp $(top_distdir)/AUTHORS \
		|| (rm -f AUTHORS.tmp ; \
		echo Failed to generate AUTHORS >&2 ); \
	else \
		echo A git clone is required to generate a ChangeLog and a AUTHORS file >&2; \
	fi

.PHONY: $(GENERATED_NEBEV)
