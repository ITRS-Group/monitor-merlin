default: all
CC = gcc
#CFLAGS = -O2 -pipe $(WARN_FLAGS) -ggdb3 -fPIC -fno-strict-aliasing -rdynamic
CFLAGS = -pipe $(WARN_FLAGS) -ggdb3 -fPIC -fno-strict-aliasing -rdynamic

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo nope')
PTHREAD_LDFLAGS = -pthread
PTHREAD_CFLAGS = -pthread
LIB_DL = -ldl
LIB_DB = -ldbi
LIB_NET =
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
	PTHREAD_LDFLAGS =
	PTHREAD_CFLAGS =
	NEEDS_MEMRCHR = YesPlease
	NEEDS_ASPRINTF = YesPlease
endif

# CFLAGS, CPPFLAGS and LDFLAGS are for users to modify
ALL_CFLAGS = $(CFLAGS) $(TWEAK_CPPFLAGS) $(CPPFLAGS) $(PTHREAD_CFLAGS)
ALL_LDFLAGS = $(LDFLAGS) $(TWEAK_LDFLAGS) $(PTHREAD_LDFLAGS)
WARN_FLAGS = -Wall -Wno-unused-parameter
#WARN_FLAGS += -Wextra# is not supported on older gcc versions.

DBWRAP_OBJS := sql.o db_wrap.o
DBWRAP_LDFLAGS :=
DBWRAP_CFLAGS :=
# FIXME: try to find libdbi/ocilib dynamically. Until then, the client
# can pass ENABLE_LIBDBI=1/0 or ENABLE_OCILIB=1/0 to enable/disable them.
ENABLE_OCILIB ?= 0
ENABLE_LIBDBI ?= 1

ifeq ($(ENABLE_OCILIB),1)
# Use ocilib...
#ORACLE_PREFIX ?= /usr/lib/oracle/11.2/client64
# /home/ora10/OraHome1
#ORACLE_LDFLAGS = -L$(ORACLE_PREFIX)/lib -lclntsh
OCILIB_PREFIX ?= /usr/local
OCILIB_CFLAGS := -I$(OCILIB_PREFIX)/include -DDB_WRAP_CONFIG_ENABLE_OCILIB=1
OCILIB_LDFLAGS = -L$(OCILIB_PREFIX)/lib -locilib $(ORACLE_LDFLAGS)
DBWRAP_CFLAGS += $(OCILIB_CFLAGS)
DBWRAP_LDFLAGS += $(OCILIB_LDFLAGS)
test-dbwrap.o db_wrap.o: CFLAGS+=$(OCILIB_CFLAGS)
db_wrap.o: db_wrap_ocilib.c
endif

ifeq ($(ENABLE_LIBDBI),1)
# Use libdbi...
LIBDBI_PREFIX ?= /usr/local
LIBDBI_CFLAGS := -I$(LIBDBI_PREFIX)/include -DDB_WRAP_CONFIG_ENABLE_LIBDBI=1
LIBDBI_LDFLAGS := -L$(LIBDBI_PREFIX)/lib -ldbi
DBWRAP_CFLAGS += $(LIBDBI_CFLAGS)
DBWRAP_LDFLAGS += $(LIBDBI_LDFLAGS)
test-dbwrap.o db_wrap.o: CFLAGS+=$(LIBDBI_CFLAGS)
db_wrap.o: db_wrap_dbi.c
endif

DBWRAP_OBJS += $(DBWRAP_OBJS)
COMMON_OBJS = cfgfile.o shared.o hash.o version.o logging.o
SHARED_OBJS = $(COMMON_OBJS) ipc.o io.o node.o codec.o binlog.o $(DBWRAP_OBJS)
TEST_OBJS = test_utils.o $(SHARED_OBJS)
DAEMON_OBJS = status.o daemonize.o daemon.o net.o $(DBWRAP_OBJS) db_updater.o state.o
DAEMON_OBJS += $(SHARED_OBJS)
MODULE_OBJS = $(SHARED_OBJS) module.o hooks.o control.o slist.o misc.o sha1.o
MODULE_DEPS = module.h hash.h slist.h
DAEMON_DEPS = net.h sql.h daemon.h hash.h
APP_OBJS = $(COMMON_OBJS) state.o logutils.o lparse.o test_utils.o
IMPORT_OBJS = $(APP_OBJS) import.o $(DBWRAP_OBJS)
SHOWLOG_OBJS = $(APP_OBJS) showlog.o auth.o
NEBTEST_OBJS = $(TEST_OBJS) nebtest.o
DEPS = Makefile cfgfile.h ipc.h logging.h shared.h
APPS = showlog import oconf
MOD_LDFLAGS = -shared -ggdb3 -fPIC $(PTHREAD_LDFLAGS)
DAEMON_LIBS = $(LIB_DB) $(LIB_NET)
DAEMON_LDFLAGS = $(DAEMON_LIBS) $(OCILIB_LDFLAGS) -ggdb3
MTEST_LIBS = $(LIB_DB) $(LIB_DL)
MTEST_LDFLAGS = $(MTEST_LIBS) $(OCILIB_LDFLAGS) -ggdb3 -rdynamic -Wl,-export-dynamic $(PTHREAD_LDFLAGS)
NEBTEST_LIBS = $(LIB_DL) $(LIB_DB) $(OCILIB_LDFLAGS)
NEBTEST_LDFLAGS = -rdynamic -Wl,-export-dynamic
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

ifndef V
	QUIET_CC    = @echo '   ' CC $@;
	QUIET_LINK  = @echo '   ' LINK $@;
endif

all: merlin.so merlind mtest nebtest $(APPS)

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

mtest: mtest.o $(DBWRAP_OBJS) $(TEST_OBJS) $(TEST_DEPS) $(MODULE_OBJS)
	$(QUIET_LINK)$(CC) $^ -o $@ $(MTEST_LDFLAGS)

test-lparse: test-lparse.o lparse.o logutils.o hash.o test_utils.o
	$(QUIET_LINK)$(CC) $^ -o $@

import: $(IMPORT_OBJS)
	$(QUIET_LINK)$(CC) $(LIB_DB) $(OCILIB_LDFLAGS) $^ -o $@

showlog: $(SHOWLOG_OBJS)
	$(QUIET_LINK)$(CC) $(LIB_DB) $^ -o $@

nebtest: $(NEBTEST_OBJS)
	$(QUIET_LINK)$(CC) $^ -o $@ $(NEBTEST_LIBS) $(NEBTEST_LDFLAGS)

merlind: $(DAEMON_OBJS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(DAEMON_LDFLAGS) $(LIBS) $^ -o $@

merlin.so: $(MODULE_OBJS)
	$(QUIET_LINK)$(CC) $(MOD_LDFLAGS) $(LDFLAGS) $^ -o $@

oconf: oconf.o sha1.o misc.o
	$(QUIET_LINK)$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(QUIET_CC)$(CC) $(ALL_CFLAGS) -c $< -o $@

#test: test-binlog test-slist test__hash test__lparse test_module
test: test-slist test__hash test__lparse test_module

test_module: nebtest merlin.so
	@./nebtest merlin.so

test__hash: test-hash
	@./test-hash

test-slist: sltest
	@./sltest

test-binlog: bltest
	@./bltest

test-hash: test-hash.o hash.o test_utils.o
	$(QUIET_LINK)$(CC) $(LDFLAGS) $^ -o $@

test__lparse: test-lparse
	@./test-lparse

sltest: sltest.o test_utils.o slist.o
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

bltest: binlog.o bltest.o test_utils.o
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $(DAEMON_LDFLAGS) $^ -o $@

bltest.o: bltest.c binlog.h

blread: blread.o codec.o $(COMMON_OBJS)

codec.o: hookinfo.h

blread.o: test/blread.c $(DEPS)
	$(QUIET_CC)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

endpoint: endpoint.o codec.o $(COMMON_OBJS)

endpoint.o: test/endpoint.c $(DEPS)
	$(QUIET_CC)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

ipc.o net.o: node.h
mtest.o nebtest.o: nagios-stubs.h

$(COMMON_OBJS): $(DEPS)
module.o: module.c $(MODULE_DEPS) $(DEPS) hash.h
$(DAEMON_OBJS): $(DAEMON_DEPS) $(DEPS)
$(MODULE_OBJS): $(MODULE_DEPS) $(DEPS)

test-dbwrap.o: test-dbwrap.c
test-dbwrap: test-dbwrap.o db_wrap.o $(SHARED_OBJS)
test-dbwrap: LDFLAGS+=$(DBWRAP_LDFLAGS)
db_wrap.o: db_wrap.h db_wrap.c
test-dbwrap: LDFLAGS+=$(MTEST_LDFLAGS)
APPS += test-dbwrap
all: test-dbwrap

version.c: gen-version.sh
	sh gen-version.sh

clean: clean-core clean-log clean-test
	rm -f $(NEB) $(PROG) $(APPS) *.o blread endpoint

clean-test:
	rm -f sltest bltest test-hash mtest test-lparse

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
