#ifndef IPC_H
#define IPC_H

#include <unistd.h>
#include <sys/un.h>

#ifndef UNIX_PATH_MAX
# define UNIX_PATH_MAX 108
#endif

extern int ipc_init(int is_module);
#define ipc_bind() ipc_init(0)
#define ipc_connect() ipc_init(1)

extern int ipc_deinit(int is_module);
#define ipc_unlink() ipc_deinit(0)
#define ipc_disconnect() ipc_init(1)

extern int ipc_grok_var(char *var, char *val);
extern int ipc_read(void *buf, size_t len, unsigned msec);
extern int ipc_write(const void *buf, size_t len, unsigned msec);
extern int ipc_send_ctrl(int control_type, int selection);
#endif
