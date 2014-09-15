AM_CFLAGS = $(CFLAGS) -I$(srcdir)/shared $(naemon_CFLAGS)
AM_CPPFLAGS = $(CPPFLAGS) -D_GNU_SOURCE

bin_PROGRAMS = merlind
pkglibexecdir = $(libexecdir)/merlin
pkglibexec_PROGRAMS = import ocimp showlog rename
pkglibexec_SCRIPTS = install-merlin.sh
bin_SCRIPTS = apps/op5

mondir = $(libdir)/merlin/mon
mon_PROGRAMS = oconf

mon_PYTHON = apps/libexec/*.py
mon_SCRIPTS = apps/libexec/*.sh apps/libexec/*.php
monmoduledir = $(mondir)/modules
monmodule_PYTHON = apps/libexec/modules/*.py
# mai gawd
monmodulepytapdir = $(monmoduledir)/pytap
monmodulepytap_PYTHON = apps/libexec/modules/pytap/*.py
monsyscheckdir = $(mondir)/syscheck
monsyscheck_SCRIPTS = apps/libexec/syscheck/*
monhelpfilesdir = $(mondir)/helpfiles
monhelpfiles_DATA = apps/libexec/helpfiles/*
monbashdir = $(mondir)/bash
monbash_SCRIPTS = apps/libexec/bash/*

pkglib_LTLIBRARIES = merlin.la

pkgconf_DATA = data/merlin.conf

sqldir = $(datarootdir)/merlin
nobase_sql_DATA = sql/update-db-1to2.sql sql/update-db-2to3.sql sql/mysql/*

naemonconf_DATA = data/merlin.cfg

merlin_la_LDFLAGS = -module -shared -fPIC
merlin_la_CFLAGS = $(AM_CFLAGS)
merlin_la_CPPFLAGS = $(AM_CPPFLAGS)
merlind_LDADD = $(naemon_LIBS) $(AM_LDADD)
merlind_CPPFLAGS = $(AM_CPPFLAGS)

initdir = $(sysconfdir)/init.d
init_SCRIPTS = data/merlind

common_sources = \
	shared/logging.c shared/logging.h \
	shared/cfgfile.c shared/cfgfile.h \
	shared/shared.c shared/shared.h \
	shared/dlist.c shared/dlist.h \
	shared/compat.h
shared_sources = $(common_sources) \
	shared/ipc.c shared/ipc.h \
	shared/io.c shared/io.h \
	shared/node.c shared/node.h \
	shared/codec.c shared/codec.h \
	shared/binlog.c shared/binlog.h

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
	shared/pgroup.c shared/pgroup.h
daemon_sources = $(shared_sources) $(db_wrap_sources) \
	daemon/status.c daemon/status.h \
	daemon/daemonize.c daemon/daemonize.h \
	daemon/net.c daemon/net.h \
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
showlog_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/daemon
showlog_LDADD = $(naemon_LIBS) $(AM_LDADD)
import_SOURCES = $(app_sources) tools/import.c $(db_wrap_sources)
import_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/daemon
import_LDADD = $(naemon_LIBS) $(AM_LDADD)
oconf_SOURCES = tools/oconf.c module/sha1.c module/misc.c
oconf_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/module
oconf_LDADD = $(naemon_LIBS) $(AM_LDADD)
ocimp_SOURCES = $(shared_sources) $(db_wrap_sources) \
	tools/ocimp.c tools/ocimp.h \
	tools/test_utils.c tools/test_utils.h \
	module/sha1.c module/sha1.h \
	tools/slist.c tools/slist.h
ocimp_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/daemon -I$(srcdir)/module
ocimp_LDADD = $(naemon_LIBS) $(AM_LDADD)
rename_SOURCES = $(app_sources) tools/rename.c $(db_wrap_sources)
rename_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/daemon
rename_LDADD = $(naemon_LIBS) $(AM_LDADD)

check_PROGRAMS = $(TESTS) test-dbwrap
TESTS = sltest test-csync test-lparse hooktest stringutilstest showlogtest bltest

sltest_SOURCES = tests/sltest.c tools/test_utils.c tools/slist.c tools/slist.h
sltest_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/tools
test_csync_SOURCES = tests/test-csync.c tools/test_utils.c $(daemon_sources)
test_csync_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/daemon -I$(srcdir)/tools
test_csync_LDADD = $(naemon_LIBS)
test_lparse_SOURCES = tests/test-lparse.c tools/lparse.c tools/logutils.c tools/test_utils.c
test_lparse_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/tools
test_lparse_CPPFLAGS = $(AM_CPPFLAGS)
test_lparse_LDADD = $(naemon_LIBS)
hooktest_SOURCES = tests/test-hooks.c shared/cfgfile.c shared/shared.c shared/logging.c shared/dlist.c shared/io.c shared/node.c shared/codec.c shared/binlog.c module/misc.c module/sha1.c tools/test_utils.c
hooktest_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/module -I$(srcdir)/tools $(check_CFLAGS)
hooktest_LDADD = $(naemon_LIBS) $(check_LIBS)
stringutilstest_SOURCES = tests/test-stringutils.c tools/test_utils.c daemon/string_utils.c
stringutilstest_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/tools -I$(srcdir)/daemon
showlogtest_SOURCES = tests/test-showlog.c $(app_sources)
showlogtest_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/tools -I$(srcdir)/daemon  $(check_CFLAGS)
showlogtest_LDADD = $(naemon_LIBS) $(check_LIBS)
bltest_SOURCES = tests/bltest.c shared/binlog.c tools/test_utils.c
bltest_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/tools

# So, it turns out that this test isn't actually a test that has ever executed
# It's just a binary that could be used for writing tests against. I give up :(
test_dbwrap_SOURCES = tests/test-dbwrap.c $(db_wrap_sources) $(shared_sources)
test_dbwrap_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/daemon
test_dbwrap_LDADD = $(naemon_LIBS) $(AM_LDADD)

install-exec-hook:
if AUTO_POSTINSTALL
	bash install-merlin.sh
else
endif
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/lib/merlin
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/log/merlin
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/run/merlin

data/merlin.cfg data/merlin.conf : % : %.in
	$(AM_V_GEN) sed \
		-e 's,@localstatedir[@],$(localstatedir),g' \
		-e 's,@libexecdir[@],$(libexecdir),g' \
		-e 's,@libdir[@],$(libdir),g' \
		-e 's,@merlin_cfgdir[@],$(pkgconfdir),g' \
		-e 's,@db_name[@],$(db_name),g' \
		-e 's,@db_type[@],$(db_type),g' \
		-e 's,@db_user[@],$(db_user),g' \
		-e 's,@db_pass[@],$(db_pass),g' \
		$< > $@

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
