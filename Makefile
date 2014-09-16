default: all
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo nope')
CC = gcc
ifeq ($(uname_S),SunOS)
	SYS_CFLAGS = -pipe $(WARN_FLAGS) -ggdb3 -fPIC -fno-strict-aliasing
else
	SYS_CFLAGS = -pipe $(WARN_FLAGS) -ggdb3 -fPIC -fno-strict-aliasing -rdynamic
endif

LIB_DL = -ldl
LIB_NET =
SYM_EXPORT = -Wl,-export-dynamic
ifeq ($(uname_S),FreeBSD)
	TWEAK_CPPFLAGS = -I/usr/local/include
	LIB_DL =
endif
ifeq ($(uname_S),OpenBSD)
	TWEAK_CPPFLAGS = -I/usr/local/include
	LIB_DL =
endif
ifeq ($(uname_S),NetBSD)
	TWEAK_CPPFLAGS = -I/usr/pkg/include
	LIB_DL =
endif
# Solaris requires additional libraries
ifeq ($(uname_S),SunOS)
	TWEAK_CPPFLAGS = -I/usr/local/include
	LIB_NET = -lnsl -lsocket -lresolv
	PTHREAD_CFLAGS =
	NEEDS_MEMRCHR = YesPlease
	NEEDS_ASPRINTF = YesPlease
	SYM_EXPORT =
endif

# CFLAGS, CPPFLAGS and LDFLAGS are for users to modify
NAGIOS_PREFIX ?= /usr/local/nagios
NAGIOS_LIBDIR ?= $(NAGIOS_PREFIX)/lib
NAGIOS_INCDIR ?= $(NAGIOS_PREFIX)/include
ALL_CFLAGS = $(CFLAGS) $(TWEAK_CPPFLAGS) $(SYS_CFLAGS) $(CPPFLAGS) $(PTHREAD_CFLAGS)
ALL_CFLAGS += -D__USE_FILE_OFFSET64
ALL_CFLAGS += -I$(NAGIOS_INCDIR) -Ishared
ALL_LDFLAGS = $(LDFLAGS) $(TWEAK_LDFLAGS)
LIBNAGIOS_LDFLAGS = -L$(NAGIOS_LIBDIR) -lnagios
WARN_FLAGS = -Wall -Wno-unused-parameter
#WARN_FLAGS += -Wextra# is not supported on older gcc versions.

DBWRAP_OBJS := daemon/sql.o daemon/db_wrap.o
DB_LDFLAGS :=
DBWRAP_CFLAGS :=
# FIXME: try to find libdbi dynamically. Until then, the client
# can pass ENABLE_LIBDBI=1/0  to enable/disable them.
ENABLE_LIBDBI ?= 1

ifeq ($(ENABLE_LIBDBI),1)
# Use libdbi...
LIBDBI_PREFIX ?= /usr/local
LIBDBI_CFLAGS := -I$(LIBDBI_PREFIX)/include -DDB_WRAP_CONFIG_ENABLE_LIBDBI=1
LIBDBI_LDFLAGS := -L$(LIBDBI_PREFIX)/lib -ldbi
DB_CFLAGS += $(LIBDBI_CFLAGS) -DENABLE_LIBDBI=1
DB_LDFLAGS += $(LIBDBI_LDFLAGS)
daemon/db_wrap.o: daemon/db_wrap_dbi.c
endif
daemon/sql.o test-dbwrap.o daemon/db_wrap.o: CFLAGS+=$(DB_CFLAGS)

COMMON_OBJS = shared/cfgfile.o shared/shared.o shared/version.o shared/logging.o shared/dlist.o
SHARED_OBJS = $(COMMON_OBJS) shared/ipc.o shared/io.o shared/node.o shared/codec.o shared/binlog.o
TEST_OBJS = tools/test_utils.o $(SHARED_OBJS)
DAEMON_OBJS = daemon/status.o daemon/daemonize.o daemon/net.o $(DBWRAP_OBJS) daemon/db_updater.o daemon/state.o daemon/string_utils.o
DAEMON_OBJS += $(SHARED_OBJS)
MODULE_OBJS = $(SHARED_OBJS) module/module.o module/hooks.o module/misc.o module/sha1.o
MODULE_OBJS += module/queries.o shared/pgroup.o
MODULE_DEPS = module/module.h shared/node.h shared/pgroup.h
DAEMON_DEPS = daemon/net.h daemon/sql.h daemon/daemon.h shared/node.h
APP_OBJS = $(COMMON_OBJS) daemon/state.o tools/logutils.o tools/lparse.o tools/test_utils.o
IMPORT_OBJS = $(APP_OBJS) tools/import.o $(DBWRAP_OBJS)
SHOWLOG_OBJS = $(APP_OBJS) tools/showlog.o tools/auth.o
RENAME_OBJS = $(APP_OBJS) tools/rename.o tools/logutils.o tools/lparse.o $(DBWRAP_OBJS)
DEPS = Makefile shared/cfgfile.h shared/ipc.h shared/mrln_logging.h shared/shared.h
APPS = showlog import oconf ocimp rename
MOD_LDFLAGS = -shared -ggdb3 -fPIC
DAEMON_LIBS = $(LIB_NET)
DAEMON_LDFLAGS = $(DAEMON_LIBS) $(DB_LDFLAGS) $(LIBNAGIOS_LDFLAGS) -ggdb3
DBTEST_LDFLAGS = $(LIB_NET) $(LIBNAGIOS_LDFLAGS) $(DB_LDFLAGS) -ggdb3
HOOKTEST_LDFLAGS = $(LIBNAGIOS_LDFLAGS) -ggdb3
STRINGUTILSTEST_LDFLAGS = -ggdb3
SPARSE_FLAGS += -I. -Wno-transparent-union -Wnoundef
DESTDIR = /tmp/merlin

ifdef NEEDS_MEMRCHR
	TWEAK_CPPFLAGS += -DNEEDS_MEMRCHR
	APP_OBJS += compat/memrchr.o
endif

ifdef NEEDS_ASPRINTF
	TWEAK_CPPFLAGS += -DNEEDS_ASPRINTF
	COMMON_OBJS += compat/asprintf.o
endif

#ifndef V
#	QUIET_CC    = @echo '   ' CC $@;
#	QUIET_LINK  = @echo '   ' LINK $@;
#endif

all: merlin.so merlind $(APPS)

thanks:
	@echo "The following people have contributed to Merlin with patches,"
	@echo "bugreports, testing or ideas in one way or another."
	@echo "Our sincerest thanks to all of you."
	@echo
	@git log | sed -n 's/.*-.*:[\t ]\(.*\)[\t ]\+<.*@.*>.*/  \1/p' | sort -u
	@echo
	@echo "To view authors sorted by contributions, use 'git shortlog -ns'"

install: all
	@echo "Installing to $(DESTDIR)"
	sh install-merlin.sh --dest-dir="$(DESTDIR)"

check:
	@for i in *.c; do sparse $(ALL_CFLAGS) $(SPARSE_FLAGS) $$i 2>&1; done | grep -v /usr/include

check_latency: check_latency.o cfgfile.o
	$(QUIET_LINK)$(CC) $^ -o $@ $(ALL_LDFLAGS)

test-csync: tests/test-csync.o tools/test_utils.o $(DAEMON_OBJS)
	$(QUIET_LINK)$(CC) $^ -o $@ $(LDFLAGS) $(DAEMON_LDFLAGS)

test-lparse: tests/test-lparse.o tools/lparse.o tools/logutils.o tools/test_utils.o
	$(QUIET_LINK)$(CC) $^ -o $@ $(LIBNAGIOS_LDFLAGS)

ocimp: tools/ocimp.o $(DBWRAP_OBJS) $(TEST_OBJS) module/sha1.o tools/slist.o
	$(QUIET_LINK)$(CC) $^ -o $@ -ggdb3 $(DB_LDFLAGS) $(LIBNAGIOS_LDFLAGS) $(LDFLAGS)

import: $(IMPORT_OBJS)
	$(QUIET_LINK)$(CC) $^ -o $@ $(LDFLAGS) $(LIB_NET) $(DB_LDFLAGS) $(LIBNAGIOS_LDFLAGS)

showlog: $(SHOWLOG_OBJS)
	$(QUIET_LINK)$(CC) $^ -o $@ $(LIB_NET) $(LIBNAGIOS_LDFLAGS)

merlind: daemon/daemon.o daemon/merlind.o $(DAEMON_OBJS)
	$(QUIET_LINK)$(CC) $^ -o $@ $(LDFLAGS) $(DAEMON_LDFLAGS)

merlin.so: $(MODULE_OBJS)
	$(QUIET_LINK)$(CC) $^ -o $@ $(MOD_LDFLAGS) $(LDFLAGS)

oconf: tools/oconf.o module/sha1.o module/misc.o
	$(QUIET_LINK)$(CC) $^ -o $@ $(LDFLAGS) $(LIBNAGIOS_LDFLAGS)

rename: $(RENAME_OBJS)
	@rm -f apps/libexec/-oconf
	$(QUIET_LINK)$(CC) $^ -o $@ -ggdb3 $(DB_LDFLAGS) $(LDFLAGS) $(LIBNAGIOS_LDFLAGS)
	@ln -s $$(pwd)/oconf apps/libexec/-oconf

# hacks, because dependency tracking is depressing
tools/%.o: tools/%.c
	$(CC) $(ALL_CFLAGS) -Idaemon -Imodule -c $< -o $@
tests/%.o: tests/%.c
	$(CC) $(ALL_CFLAGS) -Idaemon -Imodule -Itools -c $< -o $@

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

#test: test-binlog test-slist test__lparse
test: test-slist test__csync test__lparse test-hooks test-stringutils test-showlog test-dbwrap test-binlog

test-slist: sltest
	@./sltest

test-binlog: bltest
	@./bltest

test__csync: test-csync
	@./test-csync

test__lparse: test-lparse
	@./test-lparse

test-hooks:	hooktest
	@./hooktest

test-stringutils: stringutilstest
	@./stringutilstest

test-showlog: showlogtest
	@./showlogtest

sltest: tests/sltest.o tools/test_utils.o tools/slist.o
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

bltest: shared/binlog.o tests/bltest.o tools/test_utils.o
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $(DAEMON_LDFLAGS) $^ -o $@

hooktest: tests/test-hooks.o $(filter-out shared/pgroup.o, $(filter-out shared/ipc.o,$(filter-out module/hooks.o,$(filter-out module/module.o,$(MODULE_OBJS)))))
	$(QUIET_LINK)$(CC) $(CFLAGS) $^ -o $@ $(HOOKTEST_LDFLAGS) `pkg-config --libs check`

stringutilstest: tests/test-stringutils.o tools/test_utils.o daemon/string_utils.o
	$(QUIET_LINK)$(CC) $(CFLAGS) $^ -o $@ $(STRINGUTILSTEST_LDFLAGS)

showlogtest: tests/test-showlog.o
	$(QUIET_LINK)$(CC) $(CFLAGS) $^ -o $@ $(LIBNAGIOS_LDFLAGS) `pkg-config --libs check`

tests/test-showlog.o: tests/test-showlog.c showlog
	$(QUIET_CC)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@ -I$(NAGIOS_INCDIR) -Ishared -Itools `pkg-config --cflags check`

tests/test-hook.o: tests/test-hook.c
	$(QUIET_CC)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@ -I$(NAGIOS_INCDIR) `pkg-config --cflags check`

bltest.o: bltest.c binlog.h

blread: blread.o codec.o $(COMMON_OBJS)

codec.o: codec.h

blread.o: test/blread.c $(DEPS)
	$(QUIET_CC)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

endpoint: endpoint.o codec.o $(COMMON_OBJS)

endpoint.o: test/endpoint.c $(DEPS)
	$(QUIET_CC)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

ipc.o net.o: node.h
ocimp.o: ocimp.c ocimp.h

$(COMMON_OBJS): $(DEPS)
module.o: module.c $(MODULE_DEPS) $(DEPS)
$(DAEMON_OBJS): $(DAEMON_DEPS) $(DEPS)
$(MODULE_OBJS): $(MODULE_DEPS) $(DEPS)

test-dbwrap: tests/test-dbwrap.o daemon/db_wrap.o $(SHARED_OBJS)
	$(QUIET_LINK)$(CC) $^ -o $@ $(LDFLAGS) $(DBTEST_LDFLAGS)
db_wrap.o: db_wrap.h db_wrap.c

shared/version.c: gen-version.sh
	sh gen-version.sh

clean: clean-core clean-log clean-test
	rm -f merlin.so merlind $(APPS) *.o blread endpoint nagios.tmp* tests/*.o shared/*.o daemon/*.o module/*.o tools/*.o

clean-test:
	rm -f sltest bltest test-lparse

clean-core:
	rm -f core core.[0-9]*

clean-log:
	rm -f ipc.{read,write}.bin *.log

## PHONY targets
.PHONY: version.c clean clean-core clean-log

clean-dbwrap:
	-rm -f merlin.sqlite
	-mysql -umerlin -pmerlin merlin -e 'drop table if exists t;'
test-dbwrap-temp: test-dbwrap
	./test-dbwrap
test-dbwrap-notemp: test-dbwrap clean-dbwrap
	./test-dbwrap -t
.PHONY: test-dbwrap-temp test-dbwrap-notemp
