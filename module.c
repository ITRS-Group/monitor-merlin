#include "module.h"
#include "nagios/nagios.h"
#include "nagios/objects.h"
#include "nagios/statusdata.h"
#include "nagios/macros.h"
#include "nagios/perfdata.h"

time_t merlin_should_send_paths = 1;

/** code start **/
extern hostgroup *hostgroup_list;
static int mrm_reap_interval = 2;

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
static int handle_host_status(merlin_header *hdr, void *buf)
{
	struct host_struct *obj;
	merlin_host_status *st_obj = (merlin_host_status *)buf;

	obj = find_host(st_obj->name);
	if (!obj) {
		lerr("Host '%s' not found. Ignoring %s event",
		     st_obj->name, callback_name(hdr->type));
		return -1;
	}

	NET2MOD_STATE_VARS(obj, st_obj->state);
	obj->last_host_notification = st_obj->state.last_notification;
	obj->next_host_notification = st_obj->state.next_notification;
	obj->accept_passive_host_checks = st_obj->state.accept_passive_checks;
	obj->obsess_over_host = st_obj->state.obsess;
	if (hdr->type == NEBCALLBACK_HOST_CHECK_DATA && obj->perf_data) {
		update_host_performance_data(obj);
	}

	return 0;
}

static int handle_service_status(merlin_header *hdr, void *buf)
{
	service *obj;
	merlin_service_status *st_obj = (merlin_service_status *)buf;

	obj = find_service(st_obj->host_name, st_obj->service_description);
	if (!obj) {
		lerr("Service '%s' on host '%s' not found. Ignoring %s event",
		     st_obj->service_description, st_obj->host_name,
		     callback_name(hdr->type));

		return -1;
	}

	NET2MOD_STATE_VARS(obj, st_obj->state);
	obj->last_notification = st_obj->state.last_notification;
	obj->next_notification = st_obj->state.next_notification;
	obj->accept_passive_service_checks = st_obj->state.accept_passive_checks;
	obj->obsess_over_service = st_obj->state.obsess;
	if (hdr->type == NEBCALLBACK_SERVICE_CHECK_DATA && obj->perf_data) {
		update_service_performance_data(obj);
	}

	return 0;
}

int handle_external_command(merlin_header *hdr, void *buf)
{
	nebstruct_external_command_data *ds = (nebstruct_external_command_data *)buf;

	process_external_command2(ds->command_type, ds->entry_time, ds->command_args);
	return 1;
}

/* events that require status updates return 1, others return 0 */
int handle_ipc_event(merlin_event *pkt)
{
	merlin_node *node = node_by_id(pkt->hdr.selection);

	if (node) {
		node->stats.events.read++;
		node->stats.bytes.read += packet_size(pkt);
		node_log_event_count(node, 0);
	}
/*	ldebug("Inbound %s event from %s. len %d, type %d",
	       callback_name(pkt->hdr.type),
		   node ? node->name : "local Merlin daemon",
		   pkt->hdr.len, *pkt->body);
*/
	/* restore the pointers so the various handlers won't have to */
	deblockify(pkt->body, pkt->hdr.len, pkt->hdr.type);

	/*
	 * check results and status updates are handled the same,
	 * with the exception that checkresults also cause performance
	 * data to be handled.
	 */
	switch (pkt->hdr.type) {
	case NEBCALLBACK_HOST_CHECK_DATA:
	case NEBCALLBACK_HOST_STATUS_DATA:
		return handle_host_status(&pkt->hdr, pkt->body);
	case NEBCALLBACK_SERVICE_CHECK_DATA:
	case NEBCALLBACK_SERVICE_STATUS_DATA:
		return handle_service_status(&pkt->hdr, pkt->body);
	case NEBCALLBACK_EXTERNAL_COMMAND_DATA:
		return handle_external_command(&pkt->hdr, pkt->body);
	default:
		lwarn("Ignoring unrecognized/unhandled callback type: %d (%s)",
		      pkt->hdr.type, callback_name(pkt->hdr.type));
	}

	return 0;
}

static int real_ipc_reap(void)
{
	do {
		merlin_event pkt;

		if (!ipc_is_connected(0)) {
			linfo("ipc is not connected. ipc event reaping aborted");
			return 0;
		}

		while (ipc_read_event(&pkt, 1000 * is_stalling()) > 0) {
			/* control packets are handled separately */
			if (pkt.hdr.type == CTRL_PACKET) {
				handle_control(&pkt);
				continue;
			}

			handle_ipc_event(&pkt);
		}
		/*
		 * use is_stalling() > 0 here to guard
		 * against bugs in is_stalling()
		 */
	} while (is_stalling() > 0);

	ipc_log_event_count();

	return 0;
}

/* this is called from inside Nagios as a scheduled event */
static int mrm_ipc_reap(void *discard)
{
	real_ipc_reap();

	schedule_new_event(EVENT_USER_FUNCTION, TRUE,
	                   time(NULL) + mrm_reap_interval, FALSE,
	                   0, NULL, FALSE, mrm_ipc_reap, NULL, 0);

	return 0;
}


hash_table *host_hash_table;
node_selection *node_selection_by_hostname(const char *name)
{
	return hash_find(host_hash_table, name);
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
	host_hash_table = hash_init(2048);
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
		struct hostsmember_struct *m;

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

			hash_add(host_hash_table, m->host_name, sel);
		}
	}

	for (i = 0; i < nsel; i++) {
		if (!num_ents[i])
			lwarn("'%s' is a selection without hosts. Are you sure you want this?",
				  get_sel_name(i));
	}

	free(num_ents);
}

static void grok_module_compound(struct cfg_comp *comp)
{
	uint i;

	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		if (!strcmp(v->key, "ipc_reap_interval")) {
			char *endp;
			mrm_reap_interval = (int)strtoul(v->value, &endp, 0);
			if (mrm_reap_interval < 0 || *endp != '\0')
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

	if (!mrm_reap_interval)
		mrm_reap_interval = 2;
}

static void grok_daemon_compound(struct cfg_comp *comp)
{
	uint i;

	for (i = 0; i < comp->nested; i++) {
		if (!prefixcmp(comp->nest[i]->name, "database")) {
			use_database = 1;
			return;
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

extern int event_broker_options;

extern char *macro_x[MACRO_X_COUNT];
extern char *config_file;

int send_paths(void)
{
	size_t config_path_len, cache_path_len;
	char *cache_file, *status_log;
	merlin_event pkt;

	if (!merlin_should_send_paths || merlin_should_send_paths > time(NULL))
		return 0;

	cache_file = macro_x[MACRO_OBJECTCACHEFILE];
	status_log = macro_x[MACRO_STATUSDATAFILE];
	if (!config_file || !cache_file) {
		lerr("config_file or xodtemplate_cache_file not set");
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
	cache_path_len = strlen(cache_file);
	memcpy(pkt.body, config_file, config_path_len);
	pkt.hdr.len = config_path_len;
	memcpy(pkt.body + pkt.hdr.len + 1, cache_file, cache_path_len);
	pkt.hdr.len += cache_path_len + 1;
	if (status_log && *status_log) {
		memcpy(pkt.body + pkt.hdr.len + 1, status_log, strlen(status_log));
		pkt.hdr.len += strlen(status_log) + 1;
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
	 * start stalling immediately and then reap so we wait
	 * a bit while the import is running
	 */
	ctrl_stall_start();
	real_ipc_reap();
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
	if (*(int *)ds != NEBTYPE_PROCESS_EVENTLOOPSTART)
		return 0;

	/* only call this function once */
	/* We can't deregister our own call back at this time, due to a bug
           in Nagios.  Removing the deregister until such time as it is safe: */
	/* neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, post_config_init); */

	linfo("Object configuration parsed.");
	ctrl_create_object_tables();
	setup_host_hash_tables();

	/*
	 * now we register the hooks we're interested in, avoiding
	 * the huge initial burst of events Nagios otherwise spews
	 * at us when it's reading its status back in from the
	 * status.sav file (assuming state retention is enabled)
	 */
	register_merlin_hooks();

	return 0;
}

/*
 * This gets run when we create an ipc connection, or when that
 * connection is lost. A CTRL_ACTIVE packet should always be
 * the first to go through the ipc socket
 */
static int ipc_action_handler(merlin_node *node, int state)
{
	if (node != &ipc || ipc.state == state)
		return 0;

	/*
	 * we must use node_send_ctrl_active() here or we'll
	 * end up in an infinite loop in ipc_ctrl(), rapidly
	 * devouring all available stack space. Since we
	 * know we're connected anyways, we don't really
	 * need the ipc_is_connected(0) call that ipc_ctrl
	 * adds before trying to send.
	 */
	if (state == STATE_CONNECTED)
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
	 * this must come before reading configuration. It's a very
	 * cheap operation anyways and doesn't allocate any memory,
	 * so it doesn't matter if we do it needlessly.
	 */
	ipc_init_struct();

	/* if we're linked with mtest we needn't parse the configuration */
	if (flags != -1 && arg != NULL)
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
	memset(&self, 0, sizeof(self));
	self.version = MERLIN_NODEINFO_VERSION;
	self.word_size = __WORDSIZE;
	self.byte_order = __BYTE_ORDER;
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

	linfo("Merlin module %s initialized successfully", merlin_version);
	mrm_ipc_reap(NULL);

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

	return 0;
}
