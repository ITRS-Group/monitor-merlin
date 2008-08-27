#ifndef NET_H
#define NET_H

#include "protocol.h"

extern int default_port;

extern struct node *nodelist_by_selection(int sel);
extern void create_node_tree(struct node *table, unsigned n);
extern int net_resolve(const char *cp, struct in_addr *inp);
extern struct node *find_node(struct sockaddr_in *sain, const char *name);
extern int net_deinit(void);
extern int net_init(void);
extern int shoutcast(int type, void *buf, size_t len);
extern int read_all(int fd, void *buf, size_t len);
extern int net_poll(void);
extern int send_ipc_data(const struct proto_hdr *head);

#endif /* NET_H */
