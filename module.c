#include "module.h"
#include "nagios/nagios.h"
#include "nagios/objects.h"
#include "nagios/statusdata.h"
#include "nagios/macros.h"
#include "nagios/perfdata.h"
#include <pthread.h>

time_t merlin_should_send_paths = 1;

/*
 * nagios functions not included in almost-but-not-nearly-public
 * functions. We're probably not meant to call them, but being a
 * member of the Nagios core team has its benefits. Mwhahahahaha
 */
extern int xodtemplate_grab_config_info(char *main_config_file);


/** code start **/
extern hostgroup *hostgroup_list;
static int mrm_reap_interval = 2;
static pthread_t reaper_thread;
static int cancel_reaping;
static int merlin_sendpath_interval = MERLIN_SENDPATH_INTERVAL;

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
	struct old_net2mod_data old;

	obj = find_host(st_obj->name);
	if (!obj) {
		lerr("Host '%s' not found. Ignoring %s event",
		     st_obj->name, callback_name(hdr->type));
		return -1;
	}

	NET2MOD_STATE_VARS(old, obj, st_obj->state);
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
	struct old_net2mod_data old;

	obj = find_service(st_obj->host_name, st_obj->service_description);
	if (!obj) {
		lerr("Service '%s' on host '%s' not found. Ignoring %s event",
		     st_obj->service_description, st_obj->host_name,
		     callback_name(hdr->type));

		return -1;
	}

	NET2MOD_STATE_VARS(old, obj, st_obj->state);
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
int handle_ipc_event(merlin_node *node, merlin_event *pkt)
{
	if (node) {
		/*
		 * this node is obviously connected, so mark it as such,
		 * but warn about nodes with empty info that's sending
		 * us data.
		 */
		node_set_state(node, STATE_CONNECTED);
		if (!node->info.byte_order) {
			lwarn("STATE: %s is sending event data but hasn't sent %s",
				  node->name, ctrl_name(CTRL_ACTIVE));
		}
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
	merlin_decode(pkt->body, pkt->hdr.len, pkt->hdr.type);

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

/*
 * Let's hope Nagios is up to scratch wrt thread-safety
 */
static void *ipc_reaper(void *discard)
{
	/* one loop to rule them all... */
	while (!cancel_reaping) {
		int recv_result;
		merlin_event *pkt;

		/* try connecting every mrm_reap_interval */
		if (!ipc_is_connected(0)) {
			sleep(mrm_reap_interval);
			continue;
		}

		/* we block while reading */
		recv_result = node_recv(&ipc, 0);

		/* and then just loop over the received packets */
		while ((pkt = node_get_event(&ipc))) {
			merlin_node *node = node_by_id(pkt->hdr.selection);
			if (node)
				node->last_recv = time(NULL);

			/* control packets are handled separately */
			if (pkt->hdr.type == CTRL_PACKET) {
				handle_control(node, pkt);
			} else {
				handle_ipc_event(node, pkt);
			}
		}
	}

	return 0;
}

/* this is called from inside Nagios as a scheduled event */
static int mrm_ipc_reap(void *discard)
{
	while (is_stalling()) {
		sleep(is_stalling());
	}

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
#define nagios_object_cache macro_x[MACRO_OBJECTCACHEFILE]
#define nagios_status_log macro_x[MACRO_STATUSDATAFILE]
int send_paths(void)
{
	size_t config_path_len, cache_path_len;
	char *cache_file, *status_log;
	merlin_event pkt;

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

	cache_file = nagios_object_cache;
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
	if (cache_file) {
		cache_path_len = strlen(cache_file);
		memcpy(pkt.body + pkt.hdr.len + 1, cache_file, cache_path_len);
		pkt.hdr.len += cache_path_len + 1;

		if (status_log && *status_log) {
			memcpy(pkt.body + pkt.hdr.len + 1, status_log, strlen(status_log));
			pkt.hdr.len += strlen(status_log) + 1;
		}
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
	ldebug("Stalling up to %d seconds while awaiting CTRL_RESUME",
		   is_stalling());
	while (is_stalling()) {
		usleep(500);
	}
	ldebug("Stalling done");
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
	register_merlin_hooks(event_mask);

	/* now we create the ipc reaper thread and send the paths */
	result = pthread_create(&reaper_thread, NULL, ipc_reaper, NULL);
	send_paths();

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
	ctrl_set_node_actions();

	linfo("Merlin module %s initialized successfully", merlin_version);
	send_pulse(NULL);
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
	void *foo;

	linfo("Unloading Merlin module");
	cancel_reaping = 1;
	pthread_cancel(reaper_thread);
	pthread_join(reaper_thread, &foo);

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
	safe_free(ipc.ioc.buf);
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
