CC = gcc
CPPFLAGS = -I.
CFLAGS = -O2 -pipe -Wall -ggdb3
SHARED_OBJS = config.o ipc.o shared.o io.o protocol.o data.o
TEST_OBJS = test_utils.o $(SHARED_OBJS)
TEST_DEPS = test_utils.h
COMMON_OBJS = logging.o $(SHARED_OBJS)
DAEMON_OBJS = daemon.o net.o sql.o db_updater.o $(COMMON_OBJS)
MODULE_OBJS = module.o hooks.o control.o hash.o $(COMMON_OBJS)
MODULE_DEPS = module.h hash.h
DAEMON_DEPS = net.h sql.h
DEPS = Makefile config.h ipc.h logging.h shared.h types.h
DSO = merlin
PROG = $(DSO)d
NEB = $(DSO).so
MOD_LDFLAGS = -shared
DAEMON_LDFLAGS = -L/usr/lib/mysql -L/usr/lib64/mysql -lmysqlclient
SPARSE_FLAGS += -I. -Wno-transparent-union -Wnoundef

ifndef V
	QUIET_CC    = @echo '   ' CC $@;
	QUIET_LINK  = @echo '   ' LINK $@;
endif

all: $(NEB) $(PROG)

check:
	@for i in *.c; do sparse $(CFLAGS) $(SPARSE_FLAGS) $$i 2>&1; done | grep -v /usr/include

blktest: blktest.o $(TEST_OBJS) $(TEST_DEPS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) $^ -o $@

$(PROG): $(DAEMON_OBJS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(DAEMON_LDFLAGS) $(LIBS) $^ -o $@

$(NEB): $(MODULE_OBJS)
	$(QUIET_LINK)$(CC) $(MOD_LDFLAGS) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

blread: blread.o data.o $(COMMON_OBJS)

data.o: hookinfo.h

blread.o: test/blread.c $(DEPS)
	$(QUIET_CC)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

endpoint: endpoint.o data.o $(COMMON_OBJS)

endpoint.o: test/endpoint.c $(DEPS)
	$(QUIET_CC)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

ipc.o net.o: protocol.h

$(COMMON_OBJS): $(DEPS)
module.o: module.c $(MODULE_DEPS) $(DEPS) hash.h
$(DAEMON_OBJS): $(DAEMON_DEPS) $(DEPS)
$(MODULE_OBJS): $(MODULE_DEPS) $(DEPS)

clean: clean-core clean-log
	rm -f $(NEB) $(PROG) *.o blread endpoint

clean-core:
	rm -f core core.[0-9]*

clean-log:
	rm -f ipc.{read,write}.bin *.log

## PHONY targets
.PHONY: clean clean-core clean-log
