CC = gcc
CFLAGS = -O2 -pipe $(WARN_FLAGS) -ggdb3 -fPIC -fno-strict-aliasing -rdynamic
WARN_FLAGS = -Wall -Wextra -Wno-unused-parameter
COMMON_OBJS = cfgfile.o shared.o hash.o version.o logging.o
SHARED_OBJS = $(COMMON_OBJS) ipc.o io.o node.o data.o binlog.o
TEST_OBJS = test_utils.o $(SHARED_OBJS)
DAEMON_OBJS = status.o daemonize.o daemon.o net.o sql.o db_updater.o state.o
DAEMON_OBJS += $(SHARED_OBJS)
MODULE_OBJS = $(SHARED_OBJS) module.o hooks.o control.o slist.o misc.o
MODULE_DEPS = module.h hash.h slist.h
DAEMON_DEPS = net.h sql.h daemon.h hash.h
APP_OBJS = $(COMMON_OBJS) state.o logutils.o lparse.o test_utils.o
IMPORT_OBJS = $(APP_OBJS) import.o sql.o
SHOWLOG_OBJS = $(APP_OBJS) showlog.o auth.o
NEBTEST_OBJS = $(TEST_OBJS) nebtest.o
DEPS = Makefile cfgfile.h ipc.h logging.h shared.h
DSO = merlin
PROG = $(DSO)d
NEB = $(DSO).so
APPS = showlog import
MOD_LDFLAGS = -shared -ggdb3 -fPIC
DAEMON_LDFLAGS = -ldbi -ggdb3 -rdynamic -Wl,-export-dynamic
MTEST_LDFLAGS = -ldbi -ggdb3 -ldl -rdynamic -Wl,-export-dynamic
SPARSE_FLAGS += -I. -Wno-transparent-union -Wnoundef
DESTDIR = /tmp/merlin

ifndef V
	QUIET_CC    = @echo '   ' CC $@;
	QUIET_LINK  = @echo '   ' LINK $@;
endif

all: $(NEB) $(PROG) mtest $(APPS)

install: all
	@echo "Installing to $(DESTDIR)"
	sh install-merlin.sh --dest-dir="$(DESTDIR)"

check:
	@for i in *.c; do sparse $(CFLAGS) $(SPARSE_FLAGS) $$i 2>&1; done | grep -v /usr/include

check_latency: check_latency.o cfgfile.o
	$(QUIET_LINK)$(CC) $^ -o $@ $(LDFLAGS)

mtest: mtest.o sql.o $(TEST_OBJS) $(TEST_DEPS) $(MODULE_OBJS)
	$(QUIET_LINK)$(CC) $^ -o $@ $(MTEST_LDFLAGS)

test-lparse: test-lparse.o lparse.o logutils.o hash.o test_utils.o
	$(QUIET_LINK)$(CC) $^ -o $@

import: $(IMPORT_OBJS)
	$(QUIET_LINK)$(CC) $^ -o $@ -ldbi

showlog: $(SHOWLOG_OBJS)
	$(QUIET_LINK)$(CC) $^ -o $@

nebtest: $(NEBTEST_OBJS)
	$(QUIET_LINK)$(CC) $^ -o $@ -ldl -rdynamic -Wl,-export-dynamic

$(PROG): $(DAEMON_OBJS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(DAEMON_LDFLAGS) $(LIBS) $^ -o $@

$(NEB): $(MODULE_OBJS)
	$(QUIET_LINK)$(CC) $(MOD_LDFLAGS) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

test: test-binlog test-slist test__hash test__lparse test_module

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

blread: blread.o data.o $(COMMON_OBJS)

data.o: hookinfo.h

blread.o: test/blread.c $(DEPS)
	$(QUIET_CC)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

endpoint: endpoint.o data.o $(COMMON_OBJS)

endpoint.o: test/endpoint.c $(DEPS)
	$(QUIET_CC)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

ipc.o net.o: node.h
mtest.o nebtest.o: nagios-stubs.h

$(COMMON_OBJS): $(DEPS)
module.o: module.c $(MODULE_DEPS) $(DEPS) hash.h
$(DAEMON_OBJS): $(DAEMON_DEPS) $(DEPS)
$(MODULE_OBJS): $(MODULE_DEPS) $(DEPS)

version.c: gen-version.sh
	sh gen-version.sh > version.c

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
