#include <nagios/lib/iobroker.h>
#include "module.h"
#include <nagios/nagios.h>
#include <nagios/objects.h>
#include <nagios/statusdata.h>
#include <nagios/macros.h>
#include <nagios/perfdata.h>
#include <nagios/comments.h>
#include <nagios/common.h>
#include <nagios/downtime.h>

merlin_node **host_check_node = NULL;
merlin_node **service_check_node = NULL;
merlin_node untracked_checks_node = {
	.name = "untracked checks",
	.type = MODE_INTERNAL,
	.host_checks = 0,
	.service_checks = 0,
};

extern iobroker_set *nagios_iobs;

time_t merlin_should_send_paths = 1;

/*
 * nagios functions not included in almost-but-not-nearly-public
 * functions. We're probably not meant to call them, but being a
 * member of the Nagios core team has its benefits. Mwhahahahaha
 */
extern int xodtemplate_grab_config_info(char *main_config_file);
extern comment *comment_list;

/** code start **/
extern hostgroup *hostgroup_list;
static int mrm_reap_interval = 2;
static int merlin_sendpath_interval = MERLIN_SENDPATH_INTERVAL;
static int db_track_current = 0;

/*
 * user-defined filters, used as or-gate. Defaults to
 * 'handle everything'. This only affects what events
 * we register callbacks for. Received events will
 * still be parsed in full.
 * It's calculated thus:
 *   event_mask = handle_events & (~ignore_events);
 * See grok_module_compound() for further details
 */
static uint32_t event_mask;

void set_host_check_node(merlin_node *node, host *h)
{
	merlin_node *old;

	old = host_check_node[h->id];
	if(old == node)
		return;

	if (!old) {
		old = &untracked_checks_node;
	}

	ldebug("Migrating hostcheck '%s' (id=%u) from %s '%s' (p-id=%u) to %s '%s' (p-id=%u)",
		   h->name, h->id,
		   node_type(old), old->name, old->peer_id,
		   node_type(node), node->name, node->peer_id);

	old->host_checks--;
	node->host_checks++;
	host_check_node[h->id] = node;
}

void set_service_check_node(merlin_node *node, service *s)
{
	merlin_node *old;

	old = service_check_node[s->id];
	if(old == node)
		return;

	if (!old) {
		old = &untracked_checks_node;
	}

	ldebug("Migrating servicecheck '%s;%s' (id=%u) from %s '%s' (p-id=%u) to %s '%s (p-id=%u)",
		   s->host_name, s->description, s->id,
		   node_type(old), old->name, old->peer_id,
		   node_type(node), node->name, node->peer_id);

	old->service_checks--;
	node->service_checks++;
	service_check_node[s->id] = node;
}

/*
 * handle_{host,service}_result() is basically identical to
 * handle_{host,service}_status(), with the added exception
 * that the check result events also cause performance data
 * to be handled by Nagios, so they're handled by the same
 * routines.
 *
 * In essence, it would probably be enough to just send the
 * check result events and ignore the rest
 */
static int handle_host_status(merlin_node *node, merlin_header *hdr, void *buf)
{
	host *obj;
	merlin_host_status *st_obj = (merlin_host_status *)buf;
	struct tmp_net2mod_data tmp;

	obj = find_host(st_obj->name);
	if (!obj) {
		lerr("Host '%s' not found. Ignoring %s event",
		     st_obj->name, callback_name(hdr->type));
		return -1;
	}

	/* discard check results that are older than our latest */
	if(obj->last_check > st_obj->state.last_check)
		return 0;

	NET2MOD_STATE_VARS(tmp, obj, st_obj->state);
	if (hdr->type == NEBCALLBACK_HOST_CHECK_DATA) {
		set_host_check_node(node, obj);
		obj->check_source = node->source_name;
		if (obj->perf_data) {
			update_host_performance_data(obj);
		}
	}

	return 0;
}

static int handle_service_status(merlin_node *node, merlin_header *hdr, void *buf)
{
	service *obj;
	merlin_service_status *st_obj = (merlin_service_status *)buf;
	struct tmp_net2mod_data tmp;

	obj = find_service(st_obj->host_name, st_obj->service_description);
	if (!obj) {
		lerr("Service '%s' on host '%s' not found. Ignoring %s event",
		     st_obj->service_description, st_obj->host_name,
		     callback_name(hdr->type));

		return -1;
	}

	/* discard check results that are older than our latest */
	if(obj->last_check > st_obj->state.last_check)
		return 0;

	NET2MOD_STATE_VARS(tmp, obj, st_obj->state);
	if (hdr->type == NEBCALLBACK_SERVICE_CHECK_DATA) {
		set_service_check_node(node, obj);
		obj->check_source = node->source_name;
		if (obj->perf_data) {
			update_service_performance_data(obj);
		}
	}

	return 0;
}

static int handle_external_command(merlin_node *node, void *buf)
{
	nebstruct_external_command_data *ds = (nebstruct_external_command_data *)buf;

	ldebug("EXTCMD: from %s: [%ld] %d;%s",
		   node->name, ds->entry_time, ds->command_type, ds->command_args);

	switch (ds->command_type) {
	case CMD_RESTART_PROCESS:
	case CMD_SHUTDOWN_PROCESS:
		/*
		 * These two are slightly dangerous, as they allow one
		 * compromised node to cause the shutdown of every
		 * node in the chain, so we simply ignore them here on
		 * the final receiving end.
		 */
		return 0;

	case CMD_DEL_HOST_COMMENT:
	case CMD_DEL_SVC_COMMENT:
		/*
		 * these we block entirely to prevent deleting the wrong
		 * comment in case the comment_id's are not in sync between
		 * nodes. COMMENT_DELETE events and the info they contain
		 * are used instead.
		 */
		return 0;
	}

	process_external_command2(ds->command_type, ds->entry_time, ds->command_args);
	return 1;
}

static int matching_comment(comment *cmnt, nebstruct_comment_data *ds)
{
	/*
	 * hash collisions can cause comments from other objects
	 * (and other types of objects) to be listed here, so we
	 * must match on host_name and service_description as
	 * well and skip comments that don't match the type
	 */
	if (cmnt->comment_type == ds->comment_type &&
		cmnt->entry_type == ds->entry_type &&
		cmnt->source == ds->source &&
		cmnt->expires == ds->expires &&
		cmnt->expire_time == ds->expire_time &&
		cmnt->entry_time == ds->entry_time &&
		cmnt->persistent == ds->persistent &&
		!strcmp(cmnt->author, ds->author_name) &&
		!strcmp(cmnt->comment_data, ds->comment_data) &&
		!strcmp(cmnt->host_name, ds->host_name) &&
		(cmnt->service_description == ds->service_description ||
		 !strcmp(cmnt->service_description, ds->service_description)))
	{
		ldebug("CMNT: cmnt->host_name: %s; ds->host_name: %s",
			   cmnt->host_name, ds->host_name);
		ldebug("CMNT: cmnt->author: %s; ds->author_name: %s",
			   cmnt->author, ds->author_name);
		ldebug("CMNT: cmnt->comment_data: %s; ds->comment_data: %s",
			   cmnt->comment_data, ds->comment_data);
		return 1;
	}

	return 0;
}

static int handle_comment_data(merlin_node *node, void *buf)
{
	nebstruct_comment_data *ds = (nebstruct_comment_data *)buf;
	unsigned long comment_id = 0;

	if (!node) {
		lerr("handle_comment_data() with NULL node? Impossible...");
		return 0;
	}

	if (ds->type == NEBTYPE_COMMENT_DELETE) {
		comment *cmnt, *next_cmnt;

		if (ds->comment_type == HOST_COMMENT) {
			cmnt = get_first_comment_by_host(ds->host_name);
			for (; cmnt; cmnt = next_cmnt) {
				next_cmnt = cmnt->nexthash;
				if (matching_comment(cmnt, ds)) {
					merlin_set_block_comment(ds);
					delete_comment(cmnt->comment_type, cmnt->comment_id);
					merlin_set_block_comment(NULL);
				}
			}
		} else {
			/* this is *really* expensive. Sort of wtf? */
			for (cmnt = comment_list; cmnt; cmnt = next_cmnt) {
				next_cmnt = cmnt->next;

				if (matching_comment(cmnt, ds)) {
					merlin_set_block_comment(ds);
					delete_comment(cmnt->comment_type, cmnt->comment_id);
					merlin_set_block_comment(NULL);
				}
			}
		}
		return 0;
	}

	/* we're adding a comment */
	merlin_set_block_comment(ds);
	add_new_comment(ds->comment_type, ds->entry_type,
	                ds->host_name, ds->service_description,
	                ds->entry_time, ds->author_name,
	                ds->comment_data, ds->persistent,
	                ds->source, ds->expires,
	                ds->expire_time, &comment_id);
	merlin_set_block_comment(NULL);


	return 0;
}

static int handle_downtime_data(merlin_node *node, void *buf)
{
	nebstruct_downtime_data *ds = (nebstruct_downtime_data *)buf;

	if (!node) {
		lerr("handle_downtime_data() with NULL node");
		return 0;
	}

	if (ds->type != NEBTYPE_DOWNTIME_DELETE && ds->type != NEBTYPE_DOWNTIME_STOP) {
		lerr("forwarded downtime event is not a delete. not good.");
		return 0;
	}

	/* the longest function name in the history of C programming... */
	delete_downtime_by_hostname_service_description_start_time_comment
		(ds->host_name, ds->service_description,
			ds->start_time,	ds->comment_data);

	return 0;
}

#define otype_agnostic_flapping_handling(obj) \
	do { \
		if (!obj) \
			return 0; \
		obj->is_flapping = starting; \
		if (!starting) { \
			comment_id = obj->flapping_comment_id; \
		} \
	} while (0)

static int handle_flapping_data(merlin_node *node, void *buf)
{
	nebstruct_flapping_data *ds = (nebstruct_flapping_data *)buf;
	unsigned long comment_id = 0;
	host *hst = NULL;
	service *srv = NULL;
	int starting, comment_type;

	if (!node) {
		lerr("handle_flapping_data() with NULL node? Weird stuff");
		return 0;
	}

	starting = ds->type == NEBTYPE_FLAPPING_START;

	if (ds->flapping_type == SERVICE_FLAPPING) {
		srv = find_service(ds->host_name, ds->service_description);
		otype_agnostic_flapping_handling(srv);
		comment_type = SERVICE_COMMENT;
	} else {
		hst = find_host(ds->host_name);
		otype_agnostic_flapping_handling(hst);
		comment_type = HOST_COMMENT;
	}

	if (!starting && comment_id) {
		delete_comment(comment_type, comment_id);
	}

	return 1;
}

/* events that require status updates return 1, others return 0 */
int handle_ipc_event(merlin_node *node, merlin_event *pkt)
{
	if (!pkt) {
		lerr("MM: pkt is NULL in handle_ipc_event()");
		return 0;
	}
	if (!pkt->body) {
		lerr("MM: pkt->body is NULL in handle_ipc_event()");
		return 0;
	}

	if (node) {
		struct timeval tv;

		/*
		 * this node is obviously connected, so mark it as such,
		 * but warn about nodes with empty info that's sending
		 * us data.
		 */
		node_set_state(node, STATE_CONNECTED, "Data received");
		if (!node->info.byte_order) {
			lwarn("STATE: %s is sending event data but hasn't sent %s",
				  node->name, ctrl_name(CTRL_ACTIVE));
			/* marker to prevent logspamming */
			node->info.byte_order = -1;
		}

		gettimeofday(&tv, NULL);
		node->latency = tv_delta_msec(&pkt->hdr.sent, &tv);
		node->stats.events.read++;
		node->stats.bytes.read += packet_size(pkt);
		node_log_event_count(node, 0);
	}
/*
	ldebug("Inbound %s event from %s. len %d, type %d",
	       callback_name(pkt->hdr.type),
		   node ? node->name : "local Merlin daemon",
		   pkt->hdr.len, *pkt->body);
*/

	/* restore the pointers so the various handlers won't have to */
	if (merlin_decode_event(node, pkt)) {
		return 0;
	}

	/*
	 * check results and status updates are handled the same,
	 * with the exception that checkresults also cause performance
	 * data to be handled.
	 */
	switch (pkt->hdr.type) {
	case NEBCALLBACK_HOST_CHECK_DATA:
	case NEBCALLBACK_HOST_STATUS_DATA:
		return handle_host_status(node, &pkt->hdr, pkt->body);
	case NEBCALLBACK_SERVICE_CHECK_DATA:
	case NEBCALLBACK_SERVICE_STATUS_DATA:
		return handle_service_status(node, &pkt->hdr, pkt->body);
	case NEBCALLBACK_EXTERNAL_COMMAND_DATA:
		return handle_external_command(node, pkt->body);
	case NEBCALLBACK_COMMENT_DATA:
		return handle_comment_data(node, pkt->body);
	case NEBCALLBACK_DOWNTIME_DATA:
		return handle_downtime_data(node, pkt->body);
	case NEBCALLBACK_FLAPPING_DATA:
		return handle_flapping_data(node, pkt->body);
	default:
		lwarn("Ignoring unrecognized/unhandled callback type: %d (%s)",
		      pkt->hdr.type, callback_name(pkt->hdr.type));
	}

	return 0;
}

static int ipc_reaper(int sd, int events, void *arg)
{
	merlin_node *source = (merlin_node *)arg;
	int recv_result;
	merlin_event *pkt;

	if ((recv_result = node_recv(source)) <= 0) {
		return 1;
	}

	/* and then just loop over the received packets */
	while ((pkt = node_get_event(source))) {
		merlin_node *node = node_by_id(pkt->hdr.selection);

		if (node) {
			int type = pkt->hdr.type == CTRL_PACKET ? NEBCALLBACK_NUMITEMS : pkt->hdr.type;
			node->last_recv = time(NULL);
			node->stats.cb_count[type].in++;
		}

		/* control packets are handled separately */
		if (pkt->hdr.type == CTRL_PACKET) {
			handle_control(node, pkt);
		} else {
			handle_ipc_event(node, pkt);
		}
	}

	return 0;
}

dkhash_table *host_hash_table;
node_selection *node_selection_by_hostname(const char *name)
{
	return dkhash_get(host_hash_table, name, NULL);
}

static void setup_host_hash_tables(void)
{
	hostgroup *hg;
	int i, nsel;
	int *num_ents = NULL;

	nsel = get_num_selections();

	/*
	 * only bother if we've got hostgroups, pollers and selections.
	 * Otherwise we'll just be wasting perfectly good memory
	 * for no good reason
	 */
	if (!hostgroup_list || !num_pollers || !nsel)
		return;

	linfo("Creating hash tables");
	host_hash_table = dkhash_create(num_objects.hosts * 1.3);
	if (!host_hash_table) {
		lerr("Failed to initialize hash tables: Out of memory");
		exit(1);
	}

	num_ents = calloc(nsel, sizeof(int));

	/*
	 * we must loop each hostgroup once, or we'll log a lot of
	 * spurious warnings that aren't exactly accurate
	 */
	for (hg = hostgroup_list; hg; hg = hg->next) {
		node_selection *sel = node_selection_by_name(hg->group_name);
		hostsmember *m;

		if (!sel)
			continue;

		for (m = hg->members; m; m = m->next) {
			node_selection *cur = node_selection_by_hostname(m->host_name);

			/*
			 * this should never happen, but if it does
			 * we just ignore it and move on
			 */
			if (cur == sel)
				continue;

			if (cur) {
				lwarn("'%s' is checked by selection '%s', so can't add to selection '%s'",
					  m->host_name, cur->name, sel->name);
				continue;
			}
			num_ents[sel->id]++;

			dkhash_insert(host_hash_table, m->host_name, NULL, sel);
		}
	}

	for (i = 0; i < nsel; i++) {
		if (!num_ents[i])
			lwarn("'%s' is a selection without hosts. Are you sure you want this?",
				  get_sel_name(i));
	}

	free(num_ents);
}

static int parse_event_filter(const char *orig_str, uint32_t *evt_mask)
{
	uint32_t mask = 0;
	char *base_str, *str, *comma;

	if (!orig_str || !*orig_str)
		return -1;

	/*
	 * initializing 'mask' to the result storage means we
	 * can let users supply the variable more times than
	 * one and get an appending result, but that could
	 * quite easily be surprising, so we don't do that
	 * just yet.
	 */

	base_str = str = strdup(orig_str);
	do {
		int code;

		while (!*str || *str == ',' || *str == ' ')
			str++;
		comma = strchr(str, ',');
		if (comma)
			*comma = 0;

		if (!strcmp(str, "all"))
			return ~0;

		code = callback_id(str);
		if (code >= 0 && code < 32) {
			mask |= 1 << code;
		} else {
			lwarn("Unable to find a callback id for '%s'\n", str);
		}

		str = comma;
		if (comma)
			*comma = ',';
	} while (str);

	free(base_str);
	*evt_mask = mask;
	return 0;
}

static void grok_module_compound(struct cfg_comp *comp)
{
	uint i;
	uint32_t handle_events = ~0; /* events to filter in */
	uint32_t ignore_events = 0;  /* events to filter out */

	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		if (!strcmp(v->key, "ipc_reap_interval")) {
			char *endp;
			mrm_reap_interval = (int)strtoul(v->value, &endp, 0);
			if (mrm_reap_interval < 0 || *endp != '\0')
				cfg_error(comp, v, "Illegal value for %s", v->key);
			continue;
		}

		/* not very widely used, I should think */
		if (!strcmp(v->key, "event_mask")) {
			event_mask = strtoul(v->value, NULL, 0);
			continue;
		}
		if (!strcmp(v->key, "handle_events")) {
			if (parse_event_filter(v->value, &handle_events) < 0)
				cfg_error(comp, v, "Illegal value for %s", v->key);
			continue;
		}
		if (!strcmp(v->key, "ignore_events")) {
			if (parse_event_filter(v->value, &ignore_events) < 0)
				cfg_error(comp, v, "Illegal value for %s", v->key);
			continue;
		}

		if (grok_common_var(comp, v))
			continue;
		if (log_grok_var(v->key, v->value))
			continue;
		if (ipc_grok_var(v->key, v->value))
			continue;

		cfg_error(comp, comp->vlist[i], "Unknown variable");
	}

	/* remove the ignored events from the handled ones */
	event_mask = handle_events & (~ignore_events);

	if (!mrm_reap_interval)
		mrm_reap_interval = 2;
}

static void grok_daemon_compound(struct cfg_comp *comp)
{
	uint i;

	for (i = 0; i < comp->nested; i++) {
		struct cfg_comp *c = comp->nest[i];
		uint vi;
		if (!prefixcmp(comp->nest[i]->name, "database")) {
			use_database = 1;
			for (vi = 0; vi < c->vars; vi++) {
				struct cfg_var *v = c->vlist[vi];
				if (!prefixcmp(v->key, "track_current")) {
					db_track_current = strtobool(v->value);
				}
			}
			break;
		}
	}
}

static void read_config(char *cfg_file)
{
	uint i;
	struct cfg_comp *config = cfg_parse_file(cfg_file);

	if (!config) {
		lwarn("Failed to read config file");
		return;
	}

	for (i = 0; i < config->vars; i++)
		grok_common_var(config, config->vlist[i]);

	for (i = 0; i < config->nested; i++) {
		struct cfg_comp *c = config->nest[i];

		if (!prefixcmp(c->name, "module")) {
			grok_module_compound(c);
			continue;
		}
		/*
		 * we sneak a peak in here only to see if we're using a
		 * database or not, as it's an important heuristic to
		 */
		if (!prefixcmp(c->name, "daemon")) {
			grok_daemon_compound(c);
			continue;
		}
	}

	/*
	 * parse all the nodes. This warns or errors out on
	 * config errors with illegal compounds as well
	 */
	node_grok_config(config);
	cfg_destroy_compound(config);
}


/* Nagios stuff goes below */
NEB_API_VERSION(CURRENT_NEB_API_VERSION);

extern int daemon_dumps_core;
void *neb_handle = NULL;

extern char *config_file;

/*
 * we send this every 15 seconds, just in case our nodes forget
 * about us. It shouldn't happen, but there are stranger things
 * than random bugs in computer programs.
 */
static int send_pulse(void *discard)
{
	node_send_ctrl_active(&ipc, CTRL_GENERIC, &self, 100);
	schedule_new_event(EVENT_USER_FUNCTION, TRUE,
	                   time(NULL) + MERLIN_PULSE_INTERVAL, FALSE,
	                   0, NULL, FALSE, send_pulse, NULL, 0);
	return 0;
}

/*
 * Sends the path to objects.cache and status.log to the
 * daemon so it can import the necessary data into the
 * database.
 */
/* check recent additions to Nagios for why these are nifty */
#define nagios_object_cache mac->x[MACRO_OBJECTCACHEFILE]
#define nagios_status_log mac->x[MACRO_STATUSDATAFILE]
int send_paths(void)
{
	size_t config_path_len, cache_path_len;
	char *cache_file, *status_log;
	merlin_event pkt;
	nagios_macros *mac;


	/*
	 * delay sending paths until we're connected, or we'll always
	 * just hang around until the stall times out and we start
	 * sending more events later, thereby triggering a new connection
	 * attempt.
	 */
	if (!ipc_is_connected(0)) {
		merlin_should_send_paths = 1;
		return 0;
	}

	if (!merlin_should_send_paths || merlin_should_send_paths > time(NULL))
		return 0;

	mac = get_global_macros();
	if (db_track_current)
		cache_file = nagios_object_cache;
	else
		asprintf(&cache_file, "/tmp/timeperiods.cache");
	status_log = nagios_status_log;

	ldebug("config_file: %p; nagios_object_cache: %p; status_log: %p",
		   config_file, cache_file, status_log);

	if (!config_file) {
		/* this should never happen. It really shouldn't */
		merlin_should_send_paths = time(NULL) + merlin_sendpath_interval;
		return -1;
	}

	memset(&pkt, 0, sizeof(pkt));
	pkt.hdr.type = CTRL_PACKET;
	pkt.hdr.code = CTRL_PATHS;
	pkt.hdr.protocol = MERLIN_PROTOCOL_VERSION;

	/*
	 * Add the paths to pkt.body as nul-terminated strings.
	 * We simply rely on 32K bytes to be enough to hold the
	 * three paths we're interested in (and they are if we're
	 * on a unixy system, where PATH_MAX is normally 4096).
	 * We cheat a little and use pkt.hdr.len as an offset
	 * to the bytestream.
	 */
	config_path_len = strlen(config_file);
	memcpy(pkt.body, config_file, config_path_len);
	pkt.hdr.len = config_path_len;
	if (cache_file && *cache_file) {
		cache_path_len = strlen(cache_file);
		memcpy(pkt.body + pkt.hdr.len + 1, cache_file, cache_path_len);
		pkt.hdr.len += cache_path_len + 1;

		if (status_log && *status_log) {
			memcpy(pkt.body + pkt.hdr.len + 1, status_log, strlen(status_log));
			pkt.hdr.len += strlen(status_log) + 1;
		}
	}

	if (!db_track_current) {
		free(cache_file);
		cache_file = NULL;
	}

	/* nul-terminate and include the nul-char */
	pkt.body[pkt.hdr.len++] = 0;
	pkt.hdr.selection = 0;

	/*
	 * if the event was successfully added to the binlog,
	 * we'll get 0 back, which means we can just let the
	 * event in the binlog be valid until the binlog gets
	 * full.
	 */
	if (ipc_send_event(&pkt) < 0)
		return -1;

	merlin_should_send_paths = 0;

	/*
	 * start stalling immediately and keep doing so until
	 * the reaper thread reads a CTRL_RESUME event so we
	 * wait until the import is completed
	 */
	ctrl_stall_start();
	return 0;
}

/*
 * This function gets called before and after Nagios has read its config
 * and written its objects.cache and status.log files.
 * We want to setup object lists and such here, so we only care about the
 * case where config has already been read.
 */
static int post_config_init(int cb, void *ds)
{
	int result;

	if (*(int *)ds != NEBTYPE_PROCESS_EVENTLOOPSTART)
		return 0;

	host_check_node = calloc(num_objects.hosts, sizeof(merlin_node *));
	service_check_node = calloc(num_objects.services, sizeof(merlin_node *));

	if (!db_track_current) {
		char *cache_file = NULL;
		FILE *fp = NULL;
		time_t current_time = 0L;
		unsigned int i;

		time(&current_time);

		asprintf(&cache_file, "/tmp/timeperiods.cache");

		/* open the cache file for writing */
		fp = fopen(cache_file, "w");
		if(fp != NULL) {
			fprintf(fp, "########################################\n");
			fprintf(fp, "#       MERLIN TIMEPERIOD CACHE FILE\n");
			fprintf(fp, "#\n");
			fprintf(fp, "# Created: %s", ctime(&current_time));
			fprintf(fp, "########################################\n\n");

			/* cache timeperiods */
			for(i = 0; i < num_objects.timeperiods; i++)
				fcache_timeperiod(fp, timeperiod_ary[i]);
			fclose(fp);
		}
		free(cache_file);
	}

	/* only call this function once */
	neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, post_config_init);

	linfo("Object configuration parsed.");
	setup_host_hash_tables();

	if((result = qh_register_handler("merlin", 0, merlin_qh)) < 0)
		lerr("Failed to register query handler: %s", strerror(-result));
	else
		linfo("merlin_qh registered with query handler");

	/*
	 * it's safe to send the hash of the config we're using now that
	 * we know the local host could parse it properly.
	 */
	send_pulse(NULL);

	/*
	 * now we register the hooks we're interested in, avoiding
	 * the huge initial burst of events Nagios otherwise spews
	 * at us when it's reading its status back in from the
	 * status.sav file (assuming state retention is enabled)
	 */
	register_merlin_hooks(event_mask);

	send_paths();

	/*
	 * this is the last event related to startup, so the regular mod hook
	 * must see it to be able to shove startup info into the database.
	 */
	merlin_mod_hook(cb, ds);

	return 0;
}

/*
 * This gets run when we create an ipc connection, or when that
 * connection is lost. A CTRL_ACTIVE packet should always be
 * the first to go through the ipc socket
 */
static int ipc_action_handler(merlin_node *node, int prev_state)
{
	if (node != &ipc || ipc.state == prev_state)
		return 0;

	/*
	 * If we get disconnected while stalling, we immediately
	 * stop stalling and note that we should send paths again.
	 * Since we never received a CTRL_RESUME we can't know for
	 * sure that the module has actually imported anything.
	 * Better safe than sorry, iow.
	 */
	if (prev_state == STATE_CONNECTED && is_stalling()) {
		ctrl_stall_stop();
		merlin_should_send_paths = 1;
	}

	if (ipc.state == STATE_CONNECTED) {
		iobroker_register(nagios_iobs, ipc.sock, (void *)&ipc, ipc_reaper);
	} else {
		iobroker_unregister(nagios_iobs, ipc.sock);
	}

	/*
	 * we must use node_send_ctrl_active() here or we'll
	 * end up in an infinite loop in ipc_ctrl(), rapidly
	 * devouring all available stack space. Since we
	 * know we're connected anyways, we don't really
	 * need the ipc_is_connected(0) call that ipc_ctrl
	 * adds before trying to send.
	 */
	if (node->state == STATE_CONNECTED)
		return node_send_ctrl_active(&ipc, CTRL_GENERIC, &self, 100);

	return 0;
}

/**
 * Initialization routine for the eventbroker module. This
 * function gets called by Nagios when it's done loading us
 */
int nebmodule_init(int flags, char *arg, nebmodule *handle)
{
	char *home = NULL;

	neb_handle = (void *)handle;

	/*
	 * this must be zero'd out before we enter the node
	 * config parsing, or we'll clobber the values collected
	 * there and think we have no nodes configured
	 */
	memset(&self, 0, sizeof(self));
	/*
	 * Solaris (among others) don't have MSG_NOSIGNAL, so we
	 * ignore SIGPIPE globally instead.
	 */
	signal(SIGPIPE, SIG_IGN);

	/*
	 * this must come before reading configuration. It's a very
	 * cheap operation anyways and doesn't allocate any memory,
	 * so it doesn't matter if we do it needlessly.
	 */
	ipc_init_struct();

	read_config(arg);

	/*
	 * Must come after reading configuration or we won't know
	 * where the logs should end up. This will leak a bit of
	 * memory, but since the user will almost certainly reload
	 * Nagios once he or she notices that Merlin doesn't work
	 * it shouldn't be much of an issue.
	 */
	if (__nagios_object_structure_version != CURRENT_OBJECT_STRUCTURE_VERSION) {
		lerr("FATAL: Nagios has a different object structure layout than expect");
		lerr("FATAL: I expected %d, but nagios reports %d.",
			 CURRENT_OBJECT_STRUCTURE_VERSION, __nagios_object_structure_version);
		lerr("FATAL: Upgrade Nagios, or recompile Merlin against the header");
		lerr("FATAL: files from the currently running Nagios in order to");
		lerr("FATAL: fix this problem.");
		return -1;
	}

	linfo("Merlin Module Loaded");

	/*
	 * now we collect info about ourselves. Somewhat akin to a
	 * capabilities and attributes list.
	 */
	self.version = MERLIN_NODEINFO_VERSION;
	self.word_size = COMPAT_WORDSIZE;
	self.byte_order = endianness();
	self.monitored_object_state_size = sizeof(monitored_object_state);
	self.object_structure_version = CURRENT_OBJECT_STRUCTURE_VERSION;
	gettimeofday(&self.start, NULL);
	self.last_cfg_change = get_last_cfg_change();
	get_config_hash(self.config_hash);

	/* make sure we can catch whatever we want */
	event_broker_options = BROKER_EVERYTHING;

	daemon_dumps_core = 1;
	home = getenv("HOME");
	if (!home)
		home = "/tmp";

	linfo("Coredumps in %s", home);
	signal(SIGSEGV, SIG_DFL);
	if (flags != -1 || arg != NULL || handle != NULL)
		chdir(home);

	/* this gets de-registered immediately, so we need to add it manually */
	neb_register_callback(NEBCALLBACK_PROCESS_DATA, neb_handle, 0, post_config_init);

	ipc.action = ipc_action_handler;
	ctrl_set_node_actions();

	linfo("Merlin module %s initialized successfully", merlin_version);

	return 0;
}


/**
 * Called by Nagios prior to the module being unloaded.
 * This function is supposed to release all pointers we've allocated
 * and make sure we reset it to a state where we can initialize it
 * later.
 */
int nebmodule_deinit(int flags, int reason)
{
	linfo("Unloading Merlin module");

	log_deinit();
	ipc_deinit();

	/* flush junk to disk */
	sync();

	deregister_merlin_hooks();

	/*
	 * free some readily available memory. Note that
	 * we leak some when we're being restarted through
	 * either SIGHUP or a PROGRAM_RESTART event sent to
	 * Nagios' command pipe. We also (currently) loose
	 * the ipc binlog, if any, which is slightly annoying
	 */
	iocache_destroy(ipc.ioc);
	safe_free(node_table);
	binlog_wipe(ipc.binlog, BINLOG_UNLINK);

	/*
	 * TODO: free the state hash tables and their data.
	 *       They're currently leaked.
	 *
	 * Requires hash api re-design so it strdup()'s the
	 * keys itself to be done properly, or a change to
	 * use the host and service descriptions passed to
	 * us from Nagios, which could well be a better
	 * solutions
	 */

	return 0;
}
