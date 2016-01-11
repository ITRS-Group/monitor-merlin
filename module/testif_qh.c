#include "testif_qh.h"
#include <naemon/naemon.h>
#include <shared/node.h>
#include <shared/shared.h>
#include <shared/ipc.h>
#include <glib.h>

/**
 * Query handler inteface for test cases. Used to override runtime parameters
 * to make it possible to have runtime parameters deterministically set, even
 * if they in production might be incorrect.
 *
 * set hash <node> <hash> - Set the hash of a node (or ipc)
 */
static void merlin_testif_udpate_hash(merlin_node *node, const char *if_name, const char *type, const unsigned char *newhash, time_t last_cfg_change) {
	if(g_strcmp0(if_name, node->name) != 0) {
		return;
	}
	if (0 == g_strcmp0(type, "expected")) {
		memcpy(node->expected.config_hash, newhash, 20);
		node->expected.last_cfg_change = last_cfg_change;
	} else if (0 == g_strcmp0(type, "info")) {
		memcpy(node->info.config_hash, newhash, 20);
		node->info.last_cfg_change = last_cfg_change;
	}
}

int merlin_testif_qh(int sd, char *buf) {
	gchar **parts = g_strsplit_set(buf, " ", 0);
	int retcode = 400;
	if( 0 == g_strcmp0(parts[0], "set") &&
		parts[1] != NULL && /* expected / info */
		0 == g_strcmp0(parts[2], "hash") &&
		parts[3] != NULL && /* node name (ipc, peer) */
		parts[4] != NULL && /* hash as hex, padded with 0 */
		parts[5] != NULL && /* last_cfg_change as integer */
		parts[6] == NULL
		) {

		unsigned int i;
		unsigned int len;
		unsigned char newhash[20];
		time_t last_cfg_change = strtoull(parts[5], NULL, 10);


		memset(newhash, 0, 20);
		len = strlen(parts[4]);
		if(len > 40)
			len = 40;
		for(i=0;i+1<len;i+=2) {
			newhash[i/2] = (g_ascii_xdigit_value(parts[4][i+0]) << 4)
				| g_ascii_xdigit_value(parts[4][i+1]);
		}

		nsock_printf(sd, "New hash: %s\n", newhash);

		merlin_testif_udpate_hash(&ipc, parts[3], parts[1], newhash, last_cfg_change);
		for(i = 0; i < num_nodes; i++) {
			merlin_testif_udpate_hash(node_table[i], parts[3], parts[1], newhash, last_cfg_change);
		}
		retcode = 200;
	}
	g_strfreev(parts);
	return retcode;
}
