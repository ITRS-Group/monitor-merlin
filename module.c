/*
 * Author: Andreas Ericsson <ae@op5.se>
 *
 * Copyright(C) 2005 OP5 AB
 * All rights reserved.
 *
 */

#define NSCORE
#include <signal.h>
#include "module.h"
#include "hash.h"
#include "ipc.h"
#include "protocol.h"
#include <nagios/nebstructs.h>
#include <nagios/objects.h>
#include <nagios/statusdata.h>

int cb_handler(int, void *);

/** code start **/
struct file_list *config_file_list;

int mode;
extern hostgroup *hostgroup_list;
#define mrm_reap_interval 5

void handle_service_result(struct proto_hdr *hdr, void *buf)
{
	service *srv;
	nebstruct_service_check_data *ds = (nebstruct_service_check_data *)buf;

	ds->host_name += (off_t)ds;
	ds->service_description += (off_t)ds;

	linfo("Received check result for service '%s' on host '%s'",
		  ds->service_description, ds->host_name);

	srv = find_service(ds->host_name, ds->service_description);
	ldebug("Located service '%s' on host '%s'",
		   srv->description, srv->host_name);

	xfree(srv->plugin_output);
	srv->plugin_output = strdup(ds->output + (off_t)ds);
	xfree(srv->perf_data);
	if (ds->perf_data)
		srv->perf_data = strdup(ds->perf_data + (off_t)ds);
	else
		srv->perf_data = NULL;

	srv->last_state = srv->current_state;
	srv->current_state = ds->state;
	srv->last_check = ds->end_time.tv_sec;
	srv->has_been_checked = 1;

	ldebug("Updating status for service '%s' on host '%s'",
		   srv->description, srv->host_name);
}


void handle_host_result(struct proto_hdr *hdr, void *buf)
{
	host *hst;

	nebstruct_host_check_data *ds = (nebstruct_host_check_data *)buf;
	ds->host_name += (off_t)ds;

	linfo("received check result for host '%s'", ds->host_name);

	hst = find_host(ds->host_name);
	ldebug("Located host '%s'", hst->name);

	xfree(hst->plugin_output);
	hst->plugin_output = strdup(ds->output + (off_t)ds);

	xfree(hst->perf_data);
	if (ds->perf_data)
		hst->perf_data = strdup(ds->perf_data + (off_t)ds);
	else
		hst->perf_data = NULL;

	hst->last_state = hst->current_state;
	hst->current_state = ds->state;
	hst->last_check = ds->end_time.tv_sec;
	hst->has_been_checked = 1;

	ldebug("Updating status for host '%s'", hst->name);
}

/* events that require status updates return 1, others return 0 */
int handle_ipc_event(struct proto_hdr *hdr, void *buf)
{
	linfo("Inbound IPC event, callback %d, len %d, type, %d",
		   hdr->type, hdr->len, *(int *)buf);

	if (!is_noc && hdr->type != CTRL_PACKET) {
		linfo("I'm a poller, so ignoring inbound non-control packet");
		return 0;
	}

	switch (hdr->type) {
	case NEBCALLBACK_HOST_STATUS_DATA:
		handle_host_result(hdr, buf);
		return 1;
	case NEBCALLBACK_SERVICE_CHECK_DATA:
		handle_service_result(hdr, buf);
		return 1;
	default:
		lwarn("Ignoring unrecognized/unhandled callback type: %d", hdr->type);
	}

	return 0;
}


static int mrm_ipc_reap(void *discard)
{
	int len, events = 0;
	struct proto_hdr hdr;

	while ((len = ipc_read(&hdr, sizeof(hdr), 0))) {
		char buf[MAX_PKT_SIZE];

		if (len != sizeof(hdr)) {
			lerr("Incomplete read(). Read %d bytes, expected %d", len, sizeof(hdr));
			break;
		}

		/* control packets are handled separately */
		if (hdr.type == CTRL_PACKET) {
			handle_control(hdr.len, hdr.selection);
			continue;
		}

		if (hdr.len > MAX_PKT_SIZE) {
			lerr("Header claims body is %d bytes. Max allowed is %d",
				 hdr.len, MAX_PKT_SIZE);
			break;
		}

		len = ipc_read(buf, hdr.len, 0);
		if (len == hdr.len)
			events += handle_ipc_event(&hdr, buf);
		else if (len < 0)
			lerr("ipc_read() failed: %s", strerror(errno));
		else if (len != hdr.len)
			lerr("Incomplete read(). Read %d bytes, expected %d", len, hdr.len);
	}

	if (events) {
		linfo("Updating status data with info from %d events", events);
		update_all_status_data();
	}

	ldebug("**** SCHEDULING NEW REAPING AT %lu", time(NULL) + mrm_reap_interval);
	schedule_new_event(EVENT_USER_FUNCTION, TRUE,
	                   time(NULL) + mrm_reap_interval, FALSE,
	                   0, NULL, FALSE, mrm_ipc_reap, NULL);

	return len;
}



/* abstract out sending headers and such fluff */
int mrm_ipc_write(const char *key, const void *buf, int len, int type)
{
	struct proto_hdr *hdr;
	int result, sel_id = -1;
	char chunk[MAX_PKT_SIZE];

	if (!buf)
		return -1;

	if (len > sizeof(chunk)) {
		ldebug("Skipping unseemly large event: %dKB\n", len >> 10);
		return -1;
	}

	if (key)
		sel_id = hash_find_val(key);

	if (!chunk)
		return -1;

	memset(chunk, 0, sizeof(chunk));
	hdr = (struct proto_hdr *)chunk;
	hdr->protocol = 0;
	hdr->type = type;
	hdr->selection = sel_id & 0xffff;
	hdr->len = len;
	gettimeofday(&hdr->sent, NULL);

	memcpy(&chunk[sizeof(struct proto_hdr)], buf, len);
	result = ipc_write(chunk, len + sizeof(struct proto_hdr), 0);

	return result;
}


static void setup_host_hash_tables(void)
{
	hostgroup *hg;
	int i, nsel;
	int *num_ents = NULL;

	linfo("Creating hash tables");
	if (!hash_init()) {
		lerr("Failed to initialize hash tables: Out of memory");
		exit(1);
	}

	nsel = get_num_selections();
	if (!nsel)
		return;

	num_ents = calloc(nsel, sizeof(int));

	ldebug("Setting up host hash tables");
	for (hg = hostgroup_list; hg; hg = hg->next) {
		int id = get_sel_id(hg->group_name);
		struct hostgroupmember_struct *m;

		if (id < 0) {
			ldebug("Hostgroup '%s' is not interesting", hg->group_name);
			continue;
		}

		ldebug("Adding hosts from hostgroup '%s'", hg->group_name);
		for (m = hg->members; m; m = m->next) {
			unsigned int sel = hash_find_val(m->host_name);

			if (sel != -1) {
				lwarn("'%s' is a member of '%s', so can't add to poller for '%s'",
					  m->host_name, get_sel_name(sel), hg->group_name);
				continue;
			}
			num_ents[id]++;
			ldebug("\tAdding host '%s'", m->host_name);
			hash_add(m->host_name, id);
		}
	}

	for (i = 0; i < nsel; i++) {
		if (!num_ents[i])
			lwarn("'%s' is a selection without hosts. Are you sure you want this?",
				  get_sel_name(i));
		else
			ldebug("Hostgroup '%s' has %d hosts", get_sel_name(i), num_ents[i]);
	}
}


static int slurp_selection(struct compound *c)
{
	int i;

	for (i = 0; i < c->vars; i++) {
		struct cfg_var *v = c->vlist[i];
		if (!v->val || strcmp(v->var, "hostgroup"))
			continue;

		add_selection(v->val);
		return 1;
	}

	return 0;
}


static void read_config(char *cfg_file)
{
	int i;
	struct compound *config = cfg_parse_file(cfg_file);

	if (!config) {
		lwarn("Failed to read config file");
		return;
	}

	for (i = 0; i < config->vars; i++)
		grok_common_var(config, config->vlist[i]);

	for (i = 0; i < config->nested; i++) {
		struct compound *c = config->nest[i];

		if (!strcmp(c->name, "module")) {
			grok_common_compound(c);
			continue;
		}
		if (is_noc && !strncmp(c->name, "poller", 6)) {
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

/* this function gets called before and after Nagios has read its config.
 * we want to setup object lists and such here, so we only care about the
 * case where config has already been read */
int post_config_init(int cb, void *ds)
{
	if (*(int *)ds != NEBTYPE_PROCESS_START)
		return 0;

	/* only call this function once */
	neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, post_config_init);

	linfo("Object configuration parsed.");
	setup_host_hash_tables();
	create_object_lists();

	if (is_noc) {
		int i;

		for (i = 0; i < get_num_selections(); i++) {
			enable_disable_checks(i, 0);
		}
	}

	mrm_ipc_reap(NULL);

	return 0;
}


/* hooks for use in the table below */
extern int hook_host_result(int cb, void *data);
extern int hook_service_result(int cb, void *data);
static struct callback_struct {
	int pollers_only;
	int type;
	int (*hook)(int, void *);
} callback_table[] = {
//	{ 1, NEBCALLBACK_PROCESS_DATA, post_config_init },
//	{ 0, NEBCALLBACK_LOG_DATA, cb_handler },
//	{ 1, NEBCALLBACK_SYSTEM_COMMAND_DATA, cb_handler },
//	{ 1, NEBCALLBACK_EVENT_HANDLER_DATA, cb_handler },
//	{ 1, NEBCALLBACK_NOTIFICATION_DATA, cb_handler },
	{ 1, NEBCALLBACK_SERVICE_CHECK_DATA, hook_service_result },
	{ 1, NEBCALLBACK_HOST_CHECK_DATA, hook_host_result },
//	{ 0, NEBCALLBACK_COMMENT_DATA, cb_handler },
//	{ 0, NEBCALLBACK_DOWNTIME_DATA, cb_handler },
//	{ NEBCALLBACK_FLAPPING_DATA, cb_handler },
//	{ 0, NEBCALLBACK_PROGRAM_STATUS_DATA, cb_handler },
//	{ 0, NEBCALLBACK_HOST_STATUS_DATA, cb_handler },
//	{ 0, NEBCALLBACK_SERVICE_STATUS_DATA, cb_handler },
};


static int mrm_ipc_connect(void *discard)
{
	int result = ipc_connect();

	if (result < 0)
		schedule_new_event(EVENT_USER_FUNCTION, TRUE, time(NULL) + 10, FALSE,
						   0, NULL, FALSE, mrm_ipc_connect, NULL);

	return result;
}


int nebmodule_init(int flags, char *arg, nebmodule *handle)
{
	char *home = NULL;
	int i;

	neb_handle = (void *)handle;

	linfo("Loading Monitor Redundancy Module");

	read_config(arg);

	/* make sure we can catch whatever we want */
	event_broker_options = BROKER_EVERYTHING;

	ldebug("Forcing coredumps");
	daemon_dumps_core = 1;
	home = getenv("HOME");
	if (!home)
		home = "/tmp";

	linfo("Coredumps in %s", home);
	signal(SIGSEGV, SIG_DFL);
	chdir(home);

	for (i = 0; i < ARRAY_SIZE(callback_table); i++) {
		struct callback_struct *cb = &callback_table[i];

		if (!is_noc || !cb->pollers_only)
			neb_register_callback(cb->type, neb_handle, 0, cb->hook);
	}

	/* this gets de-registered immediately, so we need to add it manually */
	neb_register_callback(NEBCALLBACK_PROCESS_DATA, neb_handle, 0, post_config_init);

	mrm_ipc_connect(NULL);

	return 0;
}


int nebmodule_deinit(int flags, int reason)
{
	int i;
	linfo("Unloading Monitor Redundancy Module");

	/* flush junk to disk */
	sync();

	for (i = 0; callback_table[i].hook != NULL; i++) {
		struct callback_struct *cb = &callback_table[i];
		neb_deregister_callback(cb->type, cb->hook);
	}

	return 0;
}
