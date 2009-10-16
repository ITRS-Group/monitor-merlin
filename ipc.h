#ifndef INCLUDE_ipc_h__
#define INCLUDE_ipc_h__

#include <unistd.h>
#include <sys/un.h>
#include "protocol.h"

#ifndef UNIX_PATH_MAX
# define UNIX_PATH_MAX 108
#endif

extern int ipc_init(void);
extern void ipc_deinit(void);
extern void mrm_ipc_set_connect_handler(int (*handler)(void));
extern void mrm_ipc_set_disconnect_handler(int (*handler)(void));
extern int ipc_grok_var(char *var, char *val);
extern int ipc_send_ctrl(int control_type, int selection);
extern int ipc_send_event(merlin_event *pkt);
extern int ipc_read_event(merlin_event *pkt);
extern int ipc_sock_desc(void);
extern int ipc_listen_sock_desc(void);
extern int ipc_is_connected(int msec);
extern int ipc_reinit(void);
extern int ipc_accept(void);
extern void ipc_log_event_count(void);

#endif /* INCLUDE_ipc_h__ */
