#ifndef INCLUDE_pgroup_h__
#define INCLUDE_pgroup_h__

#include <nagios/lib/bitmap.h>
#include <stdint.h>

struct merlin_node;

/* nodeflags that must be shared between all nodes in a peer group */
#define PGROUP_NODE_FLAGS (MERLIN_NODE_TAKEOVER)

#define assigned_peer(id, active_peers) (active_peers ? ((id) % (active_peers)) : 0)

/* track assigned objects */
struct merlin_assigned_objects {
	int32_t hosts, services;
};

struct merlin_peer_group {
	int id;
	struct merlin_node **nodes;
	unsigned int active_nodes;
	unsigned int total_nodes;
	unsigned int num_hostgroups;
	int overlapping;
	int flags; /* flags shared between nodes */
	/*
	 * counts for how hosts and services should be distributed
	 * Access as assign[node->pg->active_nodes][node->peer_id]
	 * to find out how many checks a node should run.
	 * When all pollers in this peer-group are offline, the
	 * checks will be distributed to the master nodes according
	 * to the same mapping.
	 */
	unsigned int alloc;
	struct merlin_assigned_objects **assign;
	struct merlin_assigned_objects **inherit;
	struct merlin_assigned_objects assigned;
	char *hostgroups;
	char **hostgroup_array;
	bitmap *host_map;
	bitmap *service_map;
	uint32_t *host_id_table;
	uint32_t *service_id_table;
};
typedef struct merlin_peer_group merlin_peer_group;

void pgroup_assign_peer_ids(merlin_peer_group *pg);
int pgroup_init(void);
void pgroup_deinit(void);
struct merlin_node *pgroup_host_node(unsigned int id);
struct merlin_node *pgroup_service_node(unsigned int id);
#endif
