#ifndef INCLUDE_net_h__
#define INCLUDE_net_h__

#include <netdb.h>
#include "shared.h"

extern unsigned short default_port;
extern unsigned int default_addr;

extern int net_deinit(void);
extern int net_init(void);
extern int net_send_ipc_data(merlin_event *pkt);
extern int net_polling_helper(fd_set *rd, fd_set *wr, int sel_val);
extern int net_is_connected(merlin_node *node);
extern int net_try_connect(merlin_node *node);
extern int net_handle_polling_results(fd_set *rd, fd_set *wr);
extern int net_sendto(merlin_node *node, merlin_event *pkt);
extern int net_sendto_many(merlin_node **ntable, uint num, merlin_event *pkt);
extern int net_input(int sd, int io_evt, void *node_);
#endif /* INCLUDE_net_h__ */
