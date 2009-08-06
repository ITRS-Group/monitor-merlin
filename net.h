#ifndef INCLUDE_net_h__
#define INCLUDE_net_h__

#include <netdb.h>
#include "protocol.h"
#include "types.h"

extern int default_port;

extern void create_node_tree(merlin_node *table, unsigned n);
extern int net_resolve(const char *cp, struct in_addr *inp);
extern merlin_node *find_node(struct sockaddr_in *sain, const char *name);
extern int net_deinit(void);
extern int net_init(void);
extern int shoutcast(int type, void *buf, size_t len);
extern int read_all(int fd, void *buf, size_t len);
extern int net_poll(void);
extern int net_send_ipc_data(merlin_event *pkt);
extern int net_sock_desc();
extern int net_polling_helper(fd_set *rd, fd_set *wr, int sel_val);
extern void check_all_node_activity(void);
extern int net_accept_one(void);
extern int net_handle_polling_results(fd_set *rd, fd_set *wr);
#endif /* INCLUDE_net_h__ */
