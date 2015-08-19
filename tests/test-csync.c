#include "module.h"
#include "ipc.h"
#include "shared.h"
#include "logging.h"
#include "test_utils.h"
#include "configuration.h"
#include "script-helpers.h"

int main(__attribute__((unused)) int argc, __attribute__((unused)) char **argv)
{
	merlin_node node;

	t_set_colors(0);
	t_verbose = 1;

	memset(&node, 0, sizeof(node));
	ipc.info.last_cfg_change = 10;
	node.info.last_cfg_change = 5;
	node.type = MODE_POLLER;
	node.name = "testelitest";

	log_grok_var("log_level", "debug");

	/* remove this line to see what's going on */
	log_grok_var("log_file", "/dev/null");


	global_csync.push.cmd = ":";
	global_csync.fetch.cmd = ":";

	printf("node.csync_last_attemp: %lu\n", node.csync_last_attempt);
	t_start("csync tests");

	/* test that we avoid pushing when "connect = no" */
	node.flags = MERLIN_NODE_DEFAULT_POLLER_FLAGS & ~MERLIN_NODE_CONNECT;
	csync_node_active(&node, -1);
	ok_int(node.csync_last_attempt, 0, "Should avoid config pushing when 'connect = no'");

	/*
	 * make sure we push when "connect = no" but pushing is
	 * explicitly configured for this node
	 */
	node.csync_num_attempts = 0;
	node.csync.push.cmd = ":";
	node.csync.configured = 1;
	csync_node_active(&node, -1);
	ok_int(node.csync_num_attempts, 1, "Should push config when 'connect = no' and node has local push config");
	node.csync.push.is_running = 0;
	csync_node_active(&node, -1);

	/* test that we avoid pushing when max attempts is reached */
	node.flags = MERLIN_NODE_DEFAULT_POLLER_FLAGS;
	node.csync_num_attempts = node.csync_max_attempts = 3;
	csync_node_active(&node, -1);
	ok_int(node.csync_last_attempt > 0, 1, "Should keep pushing config when max attempts reached");

	return t_end();
}
