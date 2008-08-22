CC = gcc
CPPFLAGS = -I.
CFLAGS = -O2 -pipe -Wall -ggdb3
COMMON_OBJS = config.o ipc.o shared.o logging.o io.o protocol.o
DAEMON_OBJS = mrd.o net.o
MODULE_OBJS = module.o data.o hooks.o control.o hash.o
MODULE_DEPS = module.h hash.h
DEPS = Makefile config.h ipc.h logging.h shared.h types.h net.h
DSO = merlin
PROG = $(DSO)d
NEB = $(DSO).so
MOD_LDFLAGS = -shared

ifndef V
	QUIET_CC    = @echo '   ' CC $@;
	QUIET_LINK  = @echo '   ' LINK $@;
endif

all: $(NEB) $(PROG)

$(PROG): $(DAEMON_OBJS) $(COMMON_OBJS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(LIBS) $^ -o $@

$(NEB): $(MODULE_OBJS) $(COMMON_OBJS)
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
