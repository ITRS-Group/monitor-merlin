#ifndef INCLUDE_module_script_helpers_h__
#define INCLUDE_module_script_helpers_h__
#include "node.h"
int import_objects(char *cfg, char *cache);
void csync_node_active(merlin_node *node, const merlin_nodeinfo *info, int delta);
void csync_fetch(merlin_node *node);
void update_cluster_config(void);
#endif
