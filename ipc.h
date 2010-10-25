#ifndef INCLUDE_ipc_h__
#define INCLUDE_ipc_h__

#include <unistd.h>
#include <sys/un.h>
#include "shared.h"

#ifndef UNIX_PATH_MAX
# define UNIX_PATH_MAX 108
#endif
extern merlin_node ipc;

extern void ipc_init_struct(void);
extern int ipc_init(void);
extern void ipc_deinit(void);
extern int ipc_grok_var(char *var, char *val);
extern int ipc_ctrl(int code, uint sel, void *data, uint32_t len);
extern int ipc_send_event(merlin_event *pkt);
extern int ipc_listen_sock_desc(void);
extern int ipc_is_connected(int msec);
extern int ipc_reinit(void);
extern int ipc_accept(void);
extern void ipc_log_event_count(void);

/*
 * we make these inlines rather than macros so the compiler
 * can do type-checking of the arguments
 */
static inline int ipc_send_ctrl_inactive(uint id)
{
	return ipc_ctrl(CTRL_INACTIVE, id, NULL, 0);
}

static inline int ipc_send_ctrl_active(uint id, merlin_nodeinfo *info)
{
	return ipc_ctrl(CTRL_ACTIVE, id, (void *)info, sizeof(*info));
}
#define ipc_send_ctrl(code, sel) ipc_ctrl(code, sel, NULL, 0)
#endif /* INCLUDE_ipc_h__ */
