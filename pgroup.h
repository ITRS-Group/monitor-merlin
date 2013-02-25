#include "node.h"

void pgroup_assign_peer_ids(merlin_peer_group *pg);
merlin_peer_group *pgroup_get_by_cshgs(char *hgs);
merlin_peer_group *pgroup_create(char *hgs);
int pgroup_add_node(merlin_peer_group *pg, merlin_node *node);
void pgroup_init(void);
void pgroup_deinit(void);
void pgroup_map_objects(void);
