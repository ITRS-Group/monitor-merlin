#include "module.h"
#include "nagios/objects.h"
#include "nagios/statusdata.h"
#include "nagios/macros.h"

int cb_handler(int, void *);
time_t merlin_should_send_paths = 1;

/** code start **/
extern hostgroup *hostgroup_list;
#define mrm_reap_interval 5

static int handle_service_result(merlin_header *hdr, void *buf)
{
	service *srv;
	nebstruct_service_check_data *ds = (nebstruct_service_check_data *)buf;

	linfo("Received check result for service '%s' on host '%s'",
		  ds->service_description, ds->host_name);

	srv = find_service(ds->host_name, ds->service_description);
	if (!srv) {
		lerr("Unable to find service '%s' on host '%s'", ds->service_description, ds->host_name);
		return 0;
	}

	xfree(srv->plugin_output);
	srv->plugin_output = strdup(ds->output);

	xfree(srv->perf_data);
	srv->perf_data = xstrdup(ds->perf_data);

	srv->last_state = srv->current_state;
	srv->current_state = ds->state;
	srv->last_check = ds->end_time.tv_sec;
	srv->has_been_checked = 1;

	linfo("Updating status for service '%s' on host '%s'",
		  srv->description, srv->host_name);

	return 1;
}


static int handle_host_result(merlin_header *hdr, void *buf)
{
	host *hst;

	nebstruct_host_check_data *ds = (nebstruct_host_check_data *)buf;

	linfo("received check result for host '%s'", ds->host_name);

	hst = find_host(ds->host_name);
	if (!hst) {
		lerr("Unable to find host '%s'", ds->host_name);
		return 0;
	}

	xfree(hst->plugin_output);
	hst->plugin_output = strdup(ds->output);

	xfree(hst->perf_data);
	hst->perf_data = xstrdup(ds->perf_data);

	hst->last_state = hst->current_state;
	hst->current_state = ds->state;
	hst->last_check = ds->end_time.tv_sec;
	hst->has_been_checked = 1;

	linfo("Updating status for host '%s'", hst->name);

	return 1;
}

/* events that require status updates return 1, others return 0 */
int handle_ipc_event(merlin_event *pkt)
{
	linfo("Inbound IPC event, callback %d, len %d, type %d",
		   pkt->hdr.type, pkt->hdr.len, *pkt->body);

	/* restore the pointers so the various handlers won't have to */
	deblockify(pkt->body, pkt->hdr.len, pkt->hdr.type);

	switch (pkt->hdr.type) {
	case NEBCALLBACK_HOST_CHECK_DATA:
		return handle_host_result(&pkt->hdr, pkt->body);
	case NEBCALLBACK_SERVICE_CHECK_DATA:
		return handle_service_result(&pkt->hdr, pkt->body);
	default:
		lwarn("Ignoring unrecognized/unhandled callback type: %d (%s)",
		      pkt->hdr.type, callback_name(pkt->hdr.type));
	}

	return 0;
}


static int mrm_ipc_reap(void *discard)
{
	int len, events = 0;
	merlin_event pkt;

	if (!ipc_is_connected(0)) {
		linfo("ipc is not connected. ipc event reaping aborted");
		return 0;
	}
	else
		linfo("Reaping ipc events");

	while ((len = ipc_read_event(&pkt)) > 0) {
		/* control packets are handled separately */
		if (pkt.hdr.type == CTRL_PACKET) {
			handle_control(&pkt);
			continue;
		}

		events += handle_ipc_event(&pkt);
	}

	if (events) {
		linfo("Updating status data with info from %d events", events);
		update_all_status_data();
	}

	ipc_log_event_count();
	linfo("Scheduling next ipc reaping at %lu", time(NULL) + mrm_reap_interval);
	schedule_new_event(EVENT_USER_FUNCTION, TRUE,
	                   time(NULL) + mrm_reap_interval, FALSE,
	                   0, NULL, FALSE, mrm_ipc_reap, NULL, 0);

	return len;
}



/* abstract out sending headers and such fluff */
int mrm_ipc_write(const char *key, merlin_event *pkt)
{
	int selection = hash_find_val(key);

	if (selection < 0) {
		lwarn("key '%s' doesn't match any possible selection\n", key);
		return -1;
	}

	pkt->hdr.selection = selection & 0xffff;
	return ipc_send_event(pkt);
}

hash_table *host_hash_table;
static void setup_host_hash_tables(void)
{
	hostgroup *hg;
	int i, nsel;
	int *num_ents = NULL;

	linfo("Creating hash tables");
	host_hash_table = hash_init(2048);
	if (!host_hash_table) {
		lerr("Failed to initialize hash tables: Out of memory");
		exit(1);
	}

	nsel = get_num_selections();
	if (!nsel)
		return;

	num_ents = calloc(nsel, sizeof(int));

	for (hg = hostgroup_list; hg; hg = hg->next) {
		int id = get_sel_id(hg->group_name);
		struct hostsmember_struct *m;

		if (id < 0) {
			continue;
		}

		for (m = hg->members; m; m = m->next) {
			unsigned int sel = hash_find_val(m->host_name);

			if (sel != -1) {
				lwarn("'%s' is a member of '%s', so can't add to poller for '%s'",
					  m->host_name, get_sel_name(sel), hg->group_name);
				continue;
			}
			num_ents[id]++;
			hash_add(host_hash_table, m->host_name, (void *)id);
		}
	}

	for (i = 0; i < nsel; i++) {
		if (!num_ents[i])
			lwarn("'%s' is a selection without hosts. Are you sure you want this?",
				  get_sel_name(i));
	}

	free(num_ents);
}


static int slurp_selection(struct cfg_comp *c)
{
	int i;

	for (i = 0; i < c->vars; i++) {
		struct cfg_var *v = c->vlist[i];
		if (!v->key || strcmp(v->key, "hostgroup"))
			continue;

		add_selection(v->value);
		return 1;
	}

	return 0;
}

static void grok_module_compound(struct cfg_comp *comp)
{
	int i;

	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		if (grok_common_var(comp, v))
			continue;
		if (log_grok_var(v->key, v->value))
			continue;

		cfg_error(comp, comp->vlist[i], "Unknown variable");
	}
}

static void read_config(char *cfg_file)
{
	int i;
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
		if (!prefixcmp(c->name, "poller")) {
			if (!slurp_selection(c))
				cfg_error(c, NULL, "Poller without 'hostgroup' statement");
		}
	}
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
	int result;

	if (!merlin_should_send_paths)
		return 0;

	cache_file = macro_x[MACRO_OBJECTCACHEFILE];
	status_log = macro_x[MACRO_STATUSDATAFILE];
	if (!config_file || !cache_file) {
		lerr("config_file or xodtemplate_cache_file not set");
		return -1;
	}

	pkt.hdr.type = CTRL_PACKET;
	pkt.hdr.code = CTRL_PATHS;
	pkt.hdr.protocol = MERLIN_PROTOCOL_VERSION;
	memset(pkt.body, 0, sizeof(pkt.body));

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

	result = ipc_send_event(&pkt);
	if (result == packet_size(&pkt))
		merlin_should_send_paths = 0;

	return result;
}

static int mark_paths_unsent(void)
{
	merlin_should_send_paths = 1;
	return 0;
}

static int mrm_ipc_connect(void *discard)
{
	int result;

	linfo("Attempting ipc connect");
	result = ipc_init();
	if (result < 0) {
		lerr("IPC connection failed. Re-scheduling to try again in 10 seconds");
		schedule_new_event(EVENT_USER_FUNCTION, TRUE, time(NULL) + 10, FALSE,
						   0, NULL, FALSE, mrm_ipc_connect, NULL, 0);
	}
	else {
		linfo("ipc successfully connected");
	}

	return result;
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
	neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, post_config_init);

	linfo("Object configuration parsed.");
	setup_host_hash_tables();
	create_object_lists();

	mrm_ipc_connect(NULL);
	mrm_ipc_reap(NULL);
	send_paths();

	/*
	 * now we register the hooks we're interested in, avoiding
	 * the huge initial burst of events Nagios otherwise spews
	 * at us when it's reading its status back in from the
	 * status.sav file (assuming state retention is enabled)
	 */
	register_merlin_hooks();

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

	read_config(arg);

	linfo("Merlin Module Loaded");

	/* make sure we can catch whatever we want */
	event_broker_options = BROKER_EVERYTHING;

	linfo("setting connect and disconnect handlers");
	mrm_ipc_set_connect_handler(send_paths);
	mrm_ipc_set_disconnect_handler(mark_paths_unsent);
	daemon_dumps_core = 1;
	home = getenv("HOME");
	if (!home)
		home = "/tmp";

	linfo("Coredumps in %s", home);
	signal(SIGSEGV, SIG_DFL);
	chdir(home);

	/* this gets de-registered immediately, so we need to add it manually */
	neb_register_callback(NEBCALLBACK_PROCESS_DATA, neb_handle, 0, post_config_init);

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
	linfo("Unloading Monitor Redundancy Module");

	log_deinit();
	ipc_deinit();

	/* flush junk to disk */
	sync();

	deregister_merlin_hooks();

	return 0;
}
