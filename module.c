#include "module.h"
#include "nagios/objects.h"
#include "nagios/statusdata.h"
#include "nagios/macros.h"

time_t merlin_should_send_paths = 1;

static int mrm_ipc_connect(void *discard);

/** code start **/
extern hostgroup *hostgroup_list;
#define mrm_reap_interval 5

/*
 * This gets called when we don't have the ipc connection and the
 * binary backlog is full. We *will* be losing messages, so we're
 * forced to run a re-import
 */
void ipc_sync_lost(void)
{
	lerr("ipc sync lost. Messages may be lost. Re-import will be triggered.");
	merlin_should_send_paths = 1;
}

static int handle_host_status(merlin_header *hdr, void *buf)
{
	struct host_struct *obj;
	merlin_host_status *st_obj = (merlin_host_status *)buf;

	obj = find_host(st_obj->name);
	if (!obj) {
		lerr("Host '%s' not found. Ignoring status update event", st_obj->name);
		return -1;
	}

	NET2MOD_STATE_VARS(obj, st_obj->state);
	obj->last_host_notification = st_obj->state.last_notification;
	obj->next_host_notification = st_obj->state.next_notification;
	obj->accept_passive_host_checks = st_obj->state.accept_passive_checks;
	obj->obsess_over_host = st_obj->state.obsess;

	return 0;
}

static int handle_service_status(merlin_header *hdr, void *buf)
{
	service *obj;
	merlin_service_status *st_obj = (merlin_service_status *)buf;

	obj = find_service(st_obj->host_name, st_obj->service_description);
	if (!obj) {
		lerr("Service '%s' on host '%s' not found. Ignoring status update event",
		     st_obj->host_name, st_obj->service_description);

		return -1;
	}

	NET2MOD_STATE_VARS(obj, st_obj->state);
	obj->last_notification = st_obj->state.last_notification;
	obj->next_notification = st_obj->state.next_notification;
	obj->accept_passive_service_checks = st_obj->state.accept_passive_checks;
	obj->obsess_over_service = st_obj->state.obsess;

	return 0;
}

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
	case NEBCALLBACK_HOST_STATUS_DATA:
		return handle_host_status(&pkt->hdr, pkt->body);
	case NEBCALLBACK_SERVICE_STATUS_DATA:
		return handle_service_status(&pkt->hdr, pkt->body);
	default:
		lwarn("Ignoring unrecognized/unhandled callback type: %d (%s)",
		      pkt->hdr.type, callback_name(pkt->hdr.type));
	}

	return 0;
}

static int real_ipc_reap(void)
{
	int events = 0;

	linfo("Reaping ipc events");

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

			events += handle_ipc_event(&pkt);
		}
		/*
		 * use is_stalling() > 0 here to guard
		 * against bugs in is_stalling()
		 */
	} while (is_stalling() > 0);

	if (events) {
		linfo("Updating status data with info from %d events", events);
		ipc_log_event_count();
		update_all_status_data();
	}

	return events;
}

/* this is called from inside Nagios as a scheduled event */
static int mrm_ipc_reap(void *discard)
{
	real_ipc_reap();

	linfo("Scheduling next ipc reaping at %lu", time(NULL) + mrm_reap_interval);
	schedule_new_event(EVENT_USER_FUNCTION, TRUE,
	                   time(NULL) + mrm_reap_interval, FALSE,
	                   0, NULL, FALSE, mrm_ipc_reap, NULL, 0);

	return 0;
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
			int *sel = hash_find_val(m->host_name);

			if (sel) {
				lwarn("'%s' is a member of '%s', so can't add to poller for '%s'",
					  m->host_name, get_sel_name(*sel), hg->group_name);
				continue;
			}
			num_ents[id]++;

			int *selection_number = malloc(sizeof(int));
			if(selection_number) {
				*selection_number = id;
			} else {
				lerr("Unable to allocate memory for selection number");
			}

			hash_add(host_hash_table, m->host_name, selection_number);
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
	uint i;

	for (i = 0; i < c->vars; i++) {
		struct cfg_var *v = c->vlist[i];
		if (!v->key || strcmp(v->key, "hostgroup"))
			continue;

		add_selection(strdup(v->value));
		return 1;
	}

	return 0;
}

static void grok_module_compound(struct cfg_comp *comp)
{
	uint i;

	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		if (grok_common_var(comp, v))
			continue;
		if (log_grok_var(v->key, v->value))
			continue;
		if (ipc_grok_var(v->key, v->value))
			continue;

		cfg_error(comp, comp->vlist[i], "Unknown variable");
	}
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
		if (!prefixcmp(c->name, "poller") || !prefixcmp(c->name, "slave")) {
			num_pollers++;
			if (!slurp_selection(c))
				cfg_error(c, NULL, "Poller without 'hostgroup' statement");
			continue;
		}
		if (!prefixcmp(c->name, "peer")) {
			num_peers++;
			continue;
		}
		if (!prefixcmp(c->name, "noc") || !prefixcmp(c->name, "master")) {
			num_nocs++;
			continue;
		}
	}
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
		mrm_ipc_reap(NULL);
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
	/* We can't deregister our own call back at this time, due to a bug
           in Nagios.  Removing the deregister until such time as it is safe: */
	/* neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, post_config_init); */

	linfo("Object configuration parsed.");
	setup_host_hash_tables();
	create_object_lists();

	mrm_ipc_connect(NULL);
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

	/* if we're linked with mtest we needn't parse the configuration */
	if (flags != -1 && arg != NULL)
		read_config(arg);

	linfo("Merlin Module Loaded");

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

	return 0;
}
