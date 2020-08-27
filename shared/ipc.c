#include <arpa/inet.h>
#include <string.h>
#include <libgen.h>

#include "shared.h"
#include "logging.h"
#include "ipc.h"
#include "io.h"
#include "node.h"
#include "encryption.h"

static int listen_sock = -1; /* for bind() and such */
static char *ipc_sock_path;
merlin_node ipc; /* the ipc node */

/*
 * this lives here since both daemon and module needs it, but
 * none of the apps should have it
 */
int dump_nodeinfo(merlin_node *n, int sd, int instance_id)
{
	merlin_nodeinfo *i;
	merlin_node_stats *s = &n->stats;
	struct merlin_assigned_objects aso;
	merlin_peer_group *pg;

	i = &n->info;
	pg = n->pgroup;
	aso.hosts = n->assigned.current.hosts + n->assigned.extra.hosts;
	aso.services = n->assigned.current.services + n->assigned.extra.services;

	nsock_printf(sd, "instance_id=%d;name=%s;source_name=%s;socket=%d;type=%s;"
				 "state=%s;peer_id=%u;flags=%d;"
				 "address=%s;port=%u;"
				 "data_timeout=%u;last_recv=%lu;last_sent=%lu;"
				 "last_conn_attempt=%lu;last_action=%d;latency=%d;"
				 "binlog_size=%u;iocache_available=%lu;"
				 "events_sent=%llu;events_read=%llu;"
				 "events_logged=%llu;events_dropped=%llu;"
				 "bytes_sent=%llu;bytes_read=%llu;"
				 "bytes_logged=%llu;bytes_dropped=%llu;"
				 "version=%u;word_size=%u;byte_order=%u;"
				 "object_structure_version=%u;start=%lu.%lu;"
				 "last_cfg_change=%lu;config_hash=%s;"
				 "expected_config_hash=%s;"
				 "self_assigned_peer_id=%u;warn_flags=%u;"
				 "active_peers=%u;configured_peers=%u;"
				 "active_pollers=%u;configured_pollers=%u;"
				 "active_masters=%u;configured_masters=%u;"
				 "host_checks_handled=%u;service_checks_handled=%u;"
				 "host_checks_executed=%u;service_checks_executed=%u;"
				 "monitored_object_state_size=%u;connect_time=%lu;"
				 "assigned_hosts=%u;assigned_services=%u;"
				 "expired_hosts=%u;expired_services=%u;"
				 "pgroup_active_nodes=%u;pgroup_total_nodes=%u;"
				 "pgroup_hosts=%u;pgroup_services=%u;"
				 "pgroup_id=%d;pgroup_hostgroups=%s;"
				 "csync_num_attempts=%d;csync_max_attempts=%d;"
				 "csync_last_attempt=%lu;"
				 "csync_push_cmd=%s;csync_push_is_running=%d;"
				 "csync_fetch_cmd=%s;csync_fetch_is_running=%d;"
				 "encrypted=%d;uuid=%s"
				 "\n",
				 instance_id,
				 n->name, n->source_name, n->sock, node_type(n),
				 node_state_name(n->state), n->peer_id, n->flags,
				 inet_ntoa(n->sain.sin_addr), ntohs(n->sain.sin_port),
				 n->data_timeout, n->last_recv, n->last_sent,
				 n->last_conn_attempt, n->last_action, n->latency,
				 binlog_size(n->binlog), nm_bufferqueue_get_available(n->bq),
				 s->events.sent, s->events.read,
				 s->events.logged, s->events.dropped,
				 s->bytes.sent, s->bytes.read,
				 s->bytes.logged, s->bytes.dropped,
				 i->version, i->word_size, i->byte_order,
				 i->object_structure_version, i->start.tv_sec, i->start.tv_usec,
				 i->last_cfg_change, tohex(i->config_hash, 20),
				 tohex(n->expected.config_hash, 20),
				 i->peer_id, n->warn_flags,
				 i->active_peers, i->configured_peers,
				 i->active_pollers, i->configured_pollers,
				 i->active_masters, i->configured_masters,
				 i->host_checks_handled, i->service_checks_handled,
				 n->host_checks, n->service_checks,
				 i->monitored_object_state_size, n->connect_time,
				 aso.hosts, aso.services,
				 n->assigned.expired.hosts, n->assigned.expired.services,
				 n->pgroup ? n->pgroup->active_nodes : 0,
				 n->pgroup ? n->pgroup->total_nodes : 0,
				 pg ? pg->assigned.hosts : 0, pg ? pg->assigned.services : 0,
				 pg ? pg->id : -1, pg ? pg->hostgroups : "",
				 n->csync_num_attempts, n->csync_max_attempts,
				 n->csync_last_attempt,
				 n->csync.push.cmd ? n->csync.push.cmd : "", n->csync.push.is_running,
				 n->csync.fetch.cmd ? n->csync.fetch.cmd : "", n->csync.fetch.is_running,
				 n->encrypted, n->uuid
				);
	return 0;
}

void ipc_init_struct(void)
{
	memset(&ipc, 0, sizeof(ipc));
	ipc.sock = -1;
	ipc.state = STATE_NONE;
	ipc.id = CTRL_GENERIC;
	ipc.type = MODE_LOCAL;
	ipc.name = "ipc";
	ipc.flags = MERLIN_NODE_DEFAULT_IPC_FLAGS;
	ipc.bq = nm_bufferqueue_create();
	if (ipc.bq == NULL) {
		lerr("Failed to create ipc io cache: %s", strerror(errno));
		/*
		 * failing to create this buffer means we can't communicate
		 * with the daemon in any sensible fashion, so we must bail
		 * out noisily when it happens
		 */
		exit(1);
	}
}

void ipc_log_event_count(void)
{
	node_log_event_count(&ipc, 0);
}

int ipc_reinit(void)
{
	ipc_deinit();

	return ipc_init();
}

int ipc_accept(void)
{
	struct sockaddr_un saun;
	socklen_t slen = sizeof(struct sockaddr_un);

	if (ipc.sock != -1) {
		lwarn("New connection inbound when one already exists. Dropping old");
		close(ipc.sock);
	}

	ipc.sock = accept(listen_sock, (struct sockaddr *)&saun, &slen);
	if (ipc.sock < 0) {
		lerr("Failed to accept() from listen_sock (%d): %s",
			 listen_sock, strerror(errno));
		return -1;
	}

	node_set_state(&ipc, STATE_NEGOTIATING, "Accepted");

	return ipc.sock;
}

static int ipc_set_sock_path(const char *path)
{
	int result;
	struct stat st;
	char *tmp;
	const char *merlin_config_dir;

	if (strlen(path) > UNIX_PATH_MAX)
		return -1;

	safe_free(ipc_sock_path);

	tmp = strdup(merlin_config_file);
	merlin_config_dir = dirname(tmp); /* < Shouldn't be freed */

	ipc_sock_path = nspath_absolute(path, merlin_config_dir);

	free(tmp);

	if (!ipc_sock_path)
		return -1;

	result = stat(path, &st);
	if (result < 0 && errno != ENOENT)
		return -1;

	if (!result && !(st.st_mode & S_IFSOCK))
		return -2;

	return 0;
}

static int ipc_set_private_key(const char * path) {
	if ( open_encryption_key(path, ipc.privkey,
				crypto_box_SECRETKEYBYTES) ) {
		lerr("Could not open ipc_privatekey: %s\n", path);
		return 1;
	}
	return 0;
}

int ipc_grok_var(char *var, char *val)
{
	if (!val)
		return 0;

	if (!strcmp(var, "ipc_socket"))
		return !ipc_set_sock_path(val);

	if (!strcmp(var, "ipc_privatekey")) {
		return !ipc_set_private_key(val);
	}

	if (!strcmp(var, "ipc_uuid")) {
		if (!valid_uuid(val)) {
			lerr("IPC UUID must be exactly %d characters\n", UUID_SIZE);
			return 0;
		}
		strcpy(ipc.uuid, val);

		return 1;
	}

	if (!strcmp(var, "ipc_binlog")) {
		lwarn("%s is deprecated. The name will always be computed.", var);
		lwarn("   Set binlog_dir to control where the file will be created");
		return 1;
	}

	if (!strcmp(var, "ipc_binlog_dir") || !strcmp(var, "ipc_backlog_dir")) {
		lwarn("%s is deprecated. Use binlog_dir instead", var);
		return 1;
	}

	return 0;
}

int ipc_init(void)
{
	struct sockaddr_un saun;
	struct sockaddr *sa = (struct sockaddr *)&saun;
	socklen_t slen;
	int quiet = 0;

	/* don't spam the logs */
	if (ipc.last_conn_attempt_logged + 30 >= time(NULL)) {
		quiet = 1;
	} else {
		ipc.last_conn_attempt_logged = time(NULL);
	}

	if (!ipc_sock_path) {
		lerr("Attempting to initialize ipc socket, but no socket path has been set\n");
		return -1;
	}

	slen = strlen(ipc_sock_path);

	if (!quiet) {
		linfo("Initializing IPC socket '%s' for %s", ipc_sock_path,
		      is_module ? "module" : "daemon");
	}

	memset(&saun, 0, sizeof(saun));
	saun.sun_family = AF_UNIX;
	memcpy(saun.sun_path, ipc_sock_path, slen);
	slen += sizeof(struct sockaddr);

	if (listen_sock == -1 || (is_module && ipc.sock == -1)) {
		listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
		if (listen_sock < 0) {
			lerr("Failed to obtain ipc socket: %s", strerror(errno));
			return -1;
		}
		merlin_set_socket_options(listen_sock, 0);
	}

	if (!is_module) {
		mode_t old_umask;
		int result;

		if (unlink(ipc_sock_path) && errno != ENOENT) {
			lerr("Failed to unlink(%s)", ipc_sock_path);
			return -1;
		}

		slen += offsetof(struct sockaddr_un, sun_path);
		/* Socket is made world writable for now */
		old_umask = umask(0);
		result = bind(listen_sock, sa, slen);
		umask(old_umask);
		if (result < 0) {
			if (!quiet) {
				lerr("Failed to bind ipc socket %d to path '%s' with len %d: %s",
					 listen_sock, ipc_sock_path, slen, strerror(errno));
			}
			close(listen_sock);
			listen_sock = -1;
			return -1;
		}

		if (listen(listen_sock, 1) < 0) {
			lerr("listen(%d, 1) failed: %s", listen_sock, strerror(errno));
			close(listen_sock);
			listen_sock = -1;
			return -1;
		}

		return 0;
	}

	/* working with the module here */
	if (connect(listen_sock, sa, slen) < 0) {
		if (errno == EISCONN)
			return 0;
		if (!quiet) {
			lerr("Failed to connect to ipc socket '%s': %s", ipc_sock_path, strerror(errno));
		}
		ipc_deinit();
		return -1;
	}
	ipc.last_conn_attempt_logged = 0;

	/* module connected successfully */
	ipc.sock = listen_sock;
	node_set_state(&ipc, STATE_CONNECTED, "Connected");

	return 0;
}


void ipc_deinit(void)
{
	node_disconnect(&ipc, "Deinitializing");

	/* avoid spurious valgrind/strace warnings */
	if (listen_sock >= 0)
		close(listen_sock);

	listen_sock = -1;

	if (!is_module)
		unlink(ipc_sock_path);
}


int ipc_is_connected(int msec)
{
	if (is_module) {
		if (ipc.sock < 0)
			return ipc_reinit() == 0;

		node_set_state(&ipc, STATE_CONNECTED, "Connected");
		return 1;
	}

	if (io_read_ok(listen_sock, msec) > 0) {
		unsigned int i;
		ipc.sock = ipc_accept();
		if (ipc.sock < 0) {
			lerr("ipc: accept() failed: %s", strerror(errno));
			return 0;
		}
		node_set_state(&ipc, STATE_CONNECTED, "Connected");
		for (i = 0; i < num_nodes; i++) {
			node_table[i]->csync_num_attempts = 0;
		}
	}

	return ipc.sock != -1;
}

int ipc_listen_sock_desc(void)
{
	return listen_sock;
}

/*
 * Sends a control packet to ipc, making sure it's connected
 * first. If data isn't null, len bytes is copied from it to
 * pkt.body
 */
int ipc_ctrl(int code, uint sel, void *data, uint32_t len)
{
	ipc_is_connected(0);
	return node_ctrl(&ipc, code, sel, data, len);
}

int ipc_send_event(merlin_event *pkt)
{
	ipc_is_connected(0);

	/* Only modules get to say when packets are sent */
	if (is_module)
		gettimeofday(&pkt->hdr.sent, NULL);

	if (node_send_event(&ipc, pkt, 0) < 0) {
		return -1;
	}

	return 0;
}
