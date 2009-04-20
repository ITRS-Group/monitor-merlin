#ifndef IPC_H
#define IPC_H

#include <unistd.h>
#include <sys/un.h>
#include "protocol.h"

#ifndef UNIX_PATH_MAX
# define UNIX_PATH_MAX 108
#endif

extern int ipc_init(void);
#define ipc_bind() ipc_init()
#define ipc_connect() ipc_init()

extern int ipc_deinit(void);
#define ipc_unlink() ipc_deinit()
#define ipc_disconnect() ipc_init()

extern int ipc_grok_var(char *var, char *val);
extern int ipc_read(void *buf, size_t len, unsigned msec);
extern int ipc_write(const void *buf, size_t len, unsigned msec);
extern int ipc_send_ctrl(int control_type, int selection);
extern int ipc_send_event(struct merlin_event *pkt);
extern int ipc_read_event(struct merlin_event *pkt);
extern int ipc_sock_desc(void);
extern int ipc_listen_sock_desc(void);
extern int ipc_is_connected(int msec);
extern int ipc_reinit(void);
extern int ipc_accept(void);
#endif
