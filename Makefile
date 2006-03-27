CC = gcc
CPPFLAGS = -I.
CFLAGS = -O2 -pipe -Wall -ggdb3
COMMON_OBJS = config.o ipc.o shared.o logging.o io.o protocol.o
DAEMON_OBJS = mrd.o net.o
MODULE_OBJS = module.o data.o hooks.o control.o hash.o
MODULE_DEPS = module.h hash.h
DEPS = Makefile config.h ipc.h logging.h shared.h types.h net.h
PROG = mrd
MOD_LDFLAGS = -shared

all: mrd mrm.so

$(PROG): $(DAEMON_OBJS) $(COMMON_OBJS)
	$(CC) $(LDFLAGS) $(LIBS) $^ -o $@

mrm.so: $(MODULE_OBJS) $(COMMON_OBJS)
	$(CC) $(MOD_LDFLAGS) $(LDFLAGS) $^ -o $@

mod: mrm.so

blread: blread.o data.o $(COMMON_OBJS)

blread.o: test/blread.c $(DEPS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

endpoint: endpoint.o data.o $(COMMON_OBJS)

endpoint.o: test/endpoint.c $(DEPS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

ipc.o net.o: protocol.h

$(COMMON_OBJS): $(DEPS)
module.o: module.c $(MODULE_DEPS) $(DEPS) hash.h
$(DAEMON_OBJS): $(DAEMON_DEPS) $(DEPS)
$(MODULE_OBJS): $(MODULE_DEPS) $(DEPS)

clean: clean-core clean-log
	rm -f *.{o,so,out} $(PROG) mrm.so blread endpoint ipc.{read,write}.bin

clean-core:
	rm -f core*

clean-log:
	rm -f ipc.{read,write}.bin *.log

## PHONY targets
.PHONY: clean touch clean-core clean-log
