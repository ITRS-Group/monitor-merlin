#ifndef INCLUDE_pgroup_h__
#define INCLUDE_pgroup_h__
#include "node.h"

void pgroup_assign_peer_ids(merlin_peer_group *pg);
int pgroup_init(void);
void pgroup_deinit(void);
merlin_node *pgroup_host_node(unsigned int id);
merlin_node *pgroup_service_node(unsigned int id);
#endif
