#include <stdarg.h>
#include <stdio.h>
#include <naemon/naemon.h>
#include <arpa/inet.h>
#include "ipc.h"
#include "configuration.h"
#include "compat.h"
#include "module.h"
#include "misc.h"
#include "dlist.h"
#include "hooks.h"
#include "codec.h"
#include "config.h"
#include "queries.h"
#include "oconfsplit.h"
#include "script-helpers.h"
#include "net.h"

merlin_node **host_check_node = NULL;
merlin_node **service_check_node = NULL;
static merlin_node untracked_checks_node = {
	.name = "untracked checks",
	.type = MODE_INTERNAL,
	.host_checks = 0,
	.service_checks = 0,
};


unsigned short default_port = 15551;
unsigned int default_addr = 0;

static timed_event **host_expiry_map, **service_expiry_map;
struct dlist_entry *expired_events;
static struct dlist_entry **expired_hosts;
static struct dlist_entry **expired_services;

struct host *merlin_recv_host;
struct service *merlin_recv_service;

/** code start **/

/*
 * the sending node, in case it triggers more
 * events while processing its packet
 */
merlin_node *merlin_sender = NULL;
merlin_event *recv_event;

/* 9 = "reason_type", 2 = host/service, 2 = last check active/passive */
struct merlin_notify_stats merlin_notify_stats[9][2][2];

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
 * this removes the necessity for linking the
 * module against database libraries while sharing
 * configuration parsing routines.
 */
int sql_config(const char *key, const char *value)
{
	return 0;
}

void set_host_check_node(merlin_node *node, host *h, int flags)
{
	merlin_node *old, *responsible;

	old = host_check_node[h->id];
	if(old == node)
		return;

	if (!old) {
		old = &untracked_checks_node;
	}

	responsible = pgroup_host_node(h->id);

	if (!flags && node != responsible && node != old) {
		if (old == &untracked_checks_node)
			linfo("Got initial hostcheck for '%s' (id=%u) from %s '%s' (p-id=%u). Responsible node is %s %s (p-id=%u)",
			      h->name, h->id,
			      node_type(node), node->name, node->peer_id,
			      node_type(responsible), responsible->name, responsible->peer_id);
		else
			lerr("Error: Migrating hostcheck '%s' (id=%u) from %s '%s' (p-id=%u) to %s '%s' (p-id=%u; sa-p-id=%u). Responsible node is %s %s (p-id=%u; sa-p-id=%u)",
			     h->name, h->id,
			     node_type(old), old->name, old->peer_id,
			     node_type(node), node->name, node->peer_id,
			     node->info.peer_id,
			     node_type(responsible), responsible->name, responsible->peer_id,
			     responsible->info.peer_id);
	}

	old->host_checks--;
	node->host_checks++;
	host_check_node[h->id] = node;
}

void set_service_check_node(merlin_node *node, service *s, int flags)
{
	merlin_node *old = NULL, *responsible = NULL;

	old = service_check_node[s->id];
	if(old == node)
		return;

	if (!old) {
		old = &untracked_checks_node;
	}

	responsible = pgroup_service_node(s->id);

	/*
	 * we only warn about active checks, since we can't control where
	 * passive checks come in
	 */
	if (!flags && node != responsible && node != old) {
		if (old == &untracked_checks_node)
			linfo("Got initial servicecheck for '%s;%s' (id=%u) from %s '%s' (p-id=%u). Should be %s %s (p-id=%u)",
			      s->host_name, s->description, s->id,
			      node_type(node), node->name, node->peer_id,
			      node_type(responsible), responsible->name, responsible->peer_id);
		else
			lerr("Error: Migrating servicecheck '%s;%s' (id=%u) from %s '%s' (p-id=%u) to %s '%s' (p-id=%u). Should go to %s %s (p-id=%u) (pg->active_nodes=%u)",
			     s->host_name, s->description, s->id,
			     node_type(old), old->name, old->peer_id,
			     node_type(node), node->name, node->peer_id,
			     node_type(responsible), responsible->name, responsible->peer_id,
			     responsible->pgroup->active_nodes);
	}

	old->service_checks--;
	node->service_checks++;
	service_check_node[s->id] = node;
}

int unexpire_host(struct host *h)
{
	/* actually unexpire, if needed */
	if (expired_hosts[h->id]) {
		struct merlin_expired_check *last;
		struct dlist_entry *le;
		le = expired_hosts[h->id];
		last = le->data;
		last->node->assigned.expired.hosts--;
		dlist_destroy_entry(&expired_events, le, free);
		expired_hosts[h->id] = NULL;
	}

	/* next expiration event is unneeded, remove it */
	if (host_expiry_map[h->id]) {
		destroy_event(host_expiry_map[h->id]);
		host_expiry_map[h->id] = NULL;
	}

	return 0;
}

int unexpire_service(struct service *s)
{
	/* actually unexpire, if needed */
	if (expired_services[s->id]) {
		struct merlin_expired_check *last;
		struct dlist_entry *le;
		le = expired_services[s->id];
		last = le->data;
		last->node->assigned.expired.services--;
		dlist_destroy_entry(&expired_events, le, free);
		expired_services[s->id] = NULL;
	}

	/* next expiration event is unneeded, remove it */
	if (service_expiry_map[s->id]) {
		destroy_event(service_expiry_map[s->id]);
		service_expiry_map[s->id] = NULL;
	}

	return 0;
}

static void expire_event(struct nm_event_execution_properties *evprop)
{
	struct merlin_expired_check *evt = (struct merlin_expired_check *)evprop->user_data;
	time_t last_check = 0, previous_check_time = 0;
	service *s = NULL;
	host *h = NULL;
	struct merlin_expired_check *last;
	int32_t *last_counter, *this_counter;
	struct dlist_entry *le;

	if(!(evprop->execution_type == EVENT_EXEC_NORMAL)) {
		free(evt);
		return;
	}

	if (evt->type == HOST_CHECK) {
		h = evt->object;
		ldebug("EXPIR: Checking event expiry for host '%s'", h->name);
		host_expiry_map[h->id] = NULL;
		last_check = h->last_check;
		le = expired_hosts[h->id];
		last = le ? le->data : NULL;
		last_counter = last ? &last->node->assigned.expired.hosts : NULL;
		this_counter = &evt->node->assigned.expired.hosts;
		previous_check_time = evt->added - check_window(h);
	} else {
		s = evt->object;
		h = s->host_ptr;
		ldebug("EXPIR: Checking event expiry for service '%s;%s'",
		       s->host_name, s->description);
		service_expiry_map[s->id] = NULL;
		last_check = s->last_check;
		le = expired_services[s->id];
		last = le ? le->data : NULL;
		last_counter = last ? &last->node->assigned.expired.services : NULL;
		this_counter = &evt->node->assigned.expired.services;
		previous_check_time = evt->added - check_window(s);
	}

	ldebug("EXPIR:  last_check=%lu; last=%p; evt->added=%lu",
	       last_check, last, evt->added);

	/*
	 * Verify that either this check, or the last one, came in.
	 * Mega-fuzzy, but if daemons or timeperiods end or start or stuff just
	 * plain happens, we'll need to be a bit lenient. If you want to stricten
	 * this up, you need to find a way around all the early returns in
	 * handle_async_{host,service}_check_result that prevents us from finding
	 * out that naemon decided to throw the result away without telling us.
	 */
	if (previous_check_time < event_start || last_check >= previous_check_time) {
		ldebug("EXPIR:  Not expired. Recovery?");
		if (last)
			(*last_counter)--;
		if (evt->type == SERVICE_CHECK) {
			unexpire_service(s);
		} else {
			unexpire_host(h);
		}
		return;
	}

	ldebug("EXPIR:   Event expired. We have an orphan check :'(");

	/* expired again on same node. Don't count twice, so just ignore */
	if (last && last->node == evt->node) {
		ldebug("EXPIR:  expired again on same node");
		free(evt);
		return;
	}

	/* if we have an old event, keep using that list entry */
	if (last) {
		ldebug("EXPIR:  I has an last");
		(*last_counter)--;
		(*this_counter)++;
		le->data = evt;
		free(last);
		return;
	}

	/*
	 * A check has expired. Ouchie. Track it and count it.
	 */
	le = dlist_insert(expired_events, evt);
	if (!le) {
		lerr("Failed to allocate memory for event expiration.\n");
		free(evt);
		return;
	}

	expired_events = le;
	(*this_counter)++;
	if (evt->type == SERVICE_CHECK) {
		expired_services[s->id] = le;
	} else {
		expired_hosts[h->id] = le;
	}

	return;
}

void schedule_expiration_event(int type, merlin_node *node, void *obj)
{
	struct merlin_expired_check *evt;
	time_t when, now;
	struct host *h = NULL;
	struct service *s = NULL;
	struct timed_event *expiry_evt;

	if (type == SERVICE_CHECK) {
		s = (struct service *)obj;
		when = service_check_timeout * 2;
		if (service_expiry_map[s->id] != NULL)
			return;
	} else {
		h = (struct host *)obj;
		when = host_check_timeout * 2;
		if (host_expiry_map[h->id] != NULL)
			return;
	}

	evt = malloc(sizeof(*evt));
	if (!evt) {
		lerr("Failed to create expiration event");
		return;
	}

	now = time(NULL);
	evt->added = now;
	evt->object = obj;
	evt->node = node;
	evt->type = type;
	when += node->data_timeout;
	expiry_evt = schedule_event(when, expire_event, evt);
	if (type == SERVICE_CHECK) {
		service_expiry_map[s->id] = expiry_evt;
	} else {
		host_expiry_map[h->id] = expiry_evt;
	}
}

/*
 * Handles merlin control events inside the module. Control events
 * that relate to cross-host communication only never reaches this.
 */
void handle_control(merlin_node *node, merlin_event *pkt)
{
	const char *ctrl;
	int prev_state, ret;
	merlin_nodeinfo *info;

	if (!pkt) {
		lerr("handle_control() called with NULL packet");
		return;
	}

	ctrl = ctrl_name(pkt->hdr.code);
	ldebug("Received control packet code %d (%s) from %s",
		  pkt->hdr.code, ctrl, node ? node->name : "local Merlin daemon");

	/* protect against bogus headers */
	if (!node && (pkt->hdr.code == CTRL_INACTIVE || pkt->hdr.code == CTRL_ACTIVE)) {
		lerr("Received %s with unknown node id %d", ctrl, pkt->hdr.selection);
		return;
	}

	switch (pkt->hdr.code) {
	case CTRL_INACTIVE:
		node_disconnect(node, "Received CTRL_INACTIVE");
		break;
	case CTRL_ACTIVE:
		/*
		 * Only mark the node as connected if the CTRL_ACTIVE packet
		 * checks out properly and the info is new. If it *is* new,
		 * or if this node has reconnected after network problems,
		 * we must re-do the peer assignment thing.
		 */
		info = (merlin_nodeinfo *)&pkt->body;
		prev_state = node->state;
		if (node_compat_cmp(node, pkt)) {
			node_disconnect(node, "Incompatible protocol");
			return;
		}
		if (node_mconf_cmp(node, info)) {
			node_disconnect(node, "Incompatible cluster configuration");
			return;
		}
		if ((ret = node_oconf_cmp(node, info))) {
			csync_node_active(node, info, ret);
			node_disconnect(node, "Incompatible object config (sync triggered)");
			return;
		} else {
			ldebug("CSYNC: %s has object config already up to date", node->name);
		}

		/* node sent info we can use, so do that */
		memcpy(&node->info, pkt->body, sizeof(node->info));
		if (prev_state != STATE_CONNECTED) {

			node_set_state(node, STATE_CONNECTED, "Received CTRL_ACTIVE");
			ldebug("NODESTATE: %s node %s just marked as connected after CTRL_ACTIVE",
				   node_type(node), node->name);
		}
		break;
	case CTRL_STALL:
	case CTRL_RESUME:
		linfo("Received (and ignoring) CTRL_{STALL,RESUME} event.");
		break;
	case CTRL_STOP:
		linfo("Received (and ignoring) CTRL_STOP event. What voodoo is this?");
		break;
	default:
		lwarn("Unknown control code: %d", pkt->hdr.code);
	}
}

/* currently only called from "handle_{host,service}_status" */
static int handle_checkresult(struct check_result *cr, monitored_object_state *st)
{
	int ret;

	/*
	 * long_plugin_output is handled separately, since Nagios
	 * is insane and escapes it directly when it parses it.
	 */
	if (st->perf_data) {
		nm_asprintf(&cr->output, "%s|%s", st->plugin_output, st->perf_data);
	} else {
		cr->output = strdup(st->plugin_output);
	}

	cr->scheduled_check = 1;
	cr->exited_ok = 1;
	cr->latency = st->latency;
	cr->start_time.tv_sec = st->last_check;
	cr->start_time.tv_usec = 0;
	cr->finish_time.tv_sec = cr->start_time.tv_sec + (time_t)st->execution_time;
	cr->finish_time.tv_usec = 1000000 * (st->execution_time - (time_t)st->execution_time);
	ret = process_check_result(cr);
	free(cr->output);
	return ret;
}

static int handle_host_result(merlin_node *node, merlin_header *hdr, void *buf)
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
	if(obj->last_check > st_obj->state.last_check) {
		ldebug("migrate: Discarding too old result/status for host '%s' from %s %s (%lu > %lu)",
		       obj->name, node_type(node), node->name, obj->last_check, st_obj->state.last_check);
		return 0;
	}

	if (hdr->type == NEBCALLBACK_HOST_CHECK_DATA) {
		struct check_result cr;
		int ret;

		init_check_result(&cr);
		cr.object_check_type = HOST_CHECK;
		cr.check_type = CHECK_TYPE_ACTIVE;
		cr.host_name = st_obj->name;
		cr.service_description = NULL;
		cr.engine = NULL;
		cr.source = node->source_name;
		/*
		 * host DOWN states must always be critical, or Nagios will
		 * sometimes translate them to be UP (yes, it's that stupid).
		 */
		cr.return_code = st_obj->state.current_state == 0 ? 0 : 2;
		merlin_recv_host = obj;
		ret = handle_checkresult(&cr, &st_obj->state);
		free(obj->long_plugin_output);
		if (st_obj->state.long_plugin_output) {
			obj->long_plugin_output = strdup(st_obj->state.long_plugin_output);
		} else {
			obj->long_plugin_output = NULL;
		}
		merlin_recv_host = NULL;
		return ret;
	} else {
		NET2MOD_STATE_VARS(tmp, obj, st_obj->state);
	}

	return 0;
}

static int handle_service_result(merlin_node *node, merlin_header *hdr, void *buf)
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
	if(obj->last_check > st_obj->state.last_check) {
		ldebug("migrate: Discarding too old result/status for '%s;%s' from %s %s (%lu > %lu)",
		       obj->host_name, obj->description, node_type(node), node->name,
		       obj->last_check, st_obj->state.last_check);
		return 0;
	}

	if (hdr->type == NEBCALLBACK_SERVICE_CHECK_DATA) {
		struct check_result cr;
		int ret;

		init_check_result(&cr);
		cr.object_check_type = SERVICE_CHECK;
		cr.check_type = CHECK_TYPE_ACTIVE;
		cr.host_name = obj->host_name;
		cr.service_description = obj->description;
		cr.return_code = st_obj->state.current_state;
		cr.engine = NULL;
		cr.source = node->source_name;
		merlin_recv_service = obj;
		ret = handle_checkresult(&cr, &st_obj->state);
		free(obj->long_plugin_output);
		if (st_obj->state.long_plugin_output) {
			obj->long_plugin_output = strdup(st_obj->state.long_plugin_output);
		} else {
			obj->long_plugin_output = NULL;
		}
		merlin_recv_service = NULL;
		return ret;
	} else {
		NET2MOD_STATE_VARS(tmp, obj, st_obj->state);
	}

	return 0;
}

static int handle_notification_data(__attribute__((unused)) merlin_node *node, void *buf)
{
	nebstruct_notification_data *ds = (nebstruct_notification_data *)buf;

	if (ds->notification_type == HOST_NOTIFICATION) {
		struct host *h = find_host(ds->host_name);
		if (!h)
			return -1;
		h->current_notification_number = (int)(uintptr_t)(ds->object_ptr);
		h->last_notification = ds->start_time.tv_sec;
		h->next_notification = ds->end_time.tv_sec;
		h->no_more_notifications = ds->start_time.tv_usec;
		add_notified_on(h, ds->state);
	} else {
		struct service *s = find_service(ds->host_name, ds->service_description);
		if (!s)
			return -1;
		s->current_notification_number = (int)(uintptr_t)(ds->object_ptr);
		s->last_notification = ds->start_time.tv_sec;
		s->next_notification = ds->end_time.tv_sec;
		s->no_more_notifications = ds->start_time.tv_usec;
		add_notified_on(s, ds->state);
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

static int handle_comment_data(merlin_node *node, merlin_header *hdr, void *buf)
{
	nebstruct_comment_data *ds = (nebstruct_comment_data *)buf;
	unsigned long comment_id = 0;

	if (!node) {
		lerr("handle_comment_data() with NULL node? Impossible...");
		return 0;
	}

	/* make sure the object this comment is for exists */
	if (!ds->service_description) {
		if (!find_host(ds->host_name)) {
			lwarn("Host '%s' not found. Ignoring %s event.",
				  ds->host_name, callback_name(hdr->type));
			return 0;
		}
	} else if (!find_service(ds->host_name, ds->service_description)) {
		lwarn("Service '%s;%s' not found. Ignoring %s event.",
		      ds->host_name, ds->service_description, callback_name(hdr->type));
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
	} else {
		/*
		 * supposed to add. Block downtime and ACK comments to avoid
		 * duplicates
		 */
		if (ds->entry_type == ACKNOWLEDGEMENT_COMMENT) {
			ldebug("COMMENTS: Received non-delete ack comment event");
			return 0;
		}
		if (ds->entry_type == DOWNTIME_COMMENT) {
			ldebug("COMMENTS: Received non-delete downtime comment event");
			return 0;
		}
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


/* Handles an event received from another node */
int handle_event(merlin_node *node, merlin_event *pkt)
{
	uint i;
	int ret = 0;

	if (!pkt) {
		lerr("MM: pkt is NULL in handle_event()");
		return 0;
	}
	if (!pkt->body) {
		lerr("MM: pkt->body is NULL in handle_event()");
		return 0;
	}

	if (!node)
		return 0;

	if (node == &ipc) {
		linfo("Received packet from &ipc. The hells?");
		return 0;
	}

	if (pkt->hdr.type == CTRL_PACKET) {
		handle_control(node, pkt);
		return 0;
	}

	if (node->state != STATE_CONNECTED) {
		/* the f*ck did that happen? An unconnected node talking to us */
		lerr("Received data from not connected node '%s'. State is %s\n",
			 node->name, node_state(node));
		return 0;
	} else if (node->type == MODE_POLLER && num_masters) {
		ldebug("Passing on event from poller %s to %d masters",
		       node->name, num_masters);
		net_sendto_many(noc_table, num_masters, pkt);
	} else if (node->type == MODE_MASTER && num_pollers) {
		/*
		 * @todo maybe we should also check if self.peer_id == 0
		 * before we forward events to our pollers. Hmm...
		 */
		if (pkt->hdr.type != NEBCALLBACK_PROGRAM_STATUS_DATA &&
		    pkt->hdr.type != NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA)
		{
			for (i = 0; i < num_pollers; i++) {
				merlin_node *n = poller_table[i];
				net_sendto(n, pkt);
			}
		}
	}

	/* let the various handler know which node sent the packet */
	pkt->hdr.selection = node->id;

	if (!node->state == STATE_CONNECTED) {
		lwarn("STATE: Discarding %s event from %s %s",
			  callback_name(pkt->hdr.type), node_type(node), node->name);
		return 0;
	}
	if (!node->info.byte_order) {
		lwarn("STATE: %s is sending event data but hasn't sent %s",
			  node->name, ctrl_name(CTRL_ACTIVE));
		/* marker to prevent logspamming */
		node->info.byte_order = -1;
	}

	node->stats.events.read++;
	node->stats.bytes.read += packet_size(pkt);
	node_log_event_count(node, 0);

	/* send to daemon before we decode */
	if (daemon_wants(pkt->hdr.type)) {
		ipc_send_event(pkt);
	}

	/* skip decoding packets we don't use */
	switch (pkt->hdr.type) {
	case NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA:
		/*
		 * We only need to forward this to our daemon.
		 * Notifications are blocked on non-sending nodes as
		 * they're spawned by incoming check results
		 */
		return 0;
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
	case NEBCALLBACK_PROCESS_DATA:
		/* These make no sense to transfer, so warn about them */
		lwarn("EVTERR: %s %s transferred %s event", node_type(node), node->name, callback_name(pkt->hdr.type));
		return 0;
	}

	/* restore the pointers so the various handlers won't have to */
	if (merlin_decode_event(node, pkt)) {
		return 0;
	}

	merlin_sender = node;
	recv_event = pkt;

	/*
	 * check results and status updates are handled the same,
	 * with the exception that checkresults also cause performance
	 * data to be handled.
	 */
	switch (pkt->hdr.type) {

	case NEBCALLBACK_HOST_CHECK_DATA:
	case NEBCALLBACK_HOST_STATUS_DATA:
		ret = handle_host_result(node, &pkt->hdr, pkt->body);
		break;
	case NEBCALLBACK_SERVICE_CHECK_DATA:
	case NEBCALLBACK_SERVICE_STATUS_DATA:
		ret = handle_service_result(node, &pkt->hdr, pkt->body);
		break;
	case NEBCALLBACK_NOTIFICATION_DATA:
		ret = handle_notification_data(node, pkt->body);
		break;
	case NEBCALLBACK_EXTERNAL_COMMAND_DATA:
		ret = handle_external_command(node, pkt->body);
		break;
	case NEBCALLBACK_COMMENT_DATA:
		ret = handle_comment_data(node, &pkt->hdr, pkt->body);
		break;
	case NEBCALLBACK_DOWNTIME_DATA:
		ret = handle_downtime_data(node, pkt->body);
		break;
	case NEBCALLBACK_FLAPPING_DATA:
		ret = handle_flapping_data(node, pkt->body);
		break;
	default:
		lwarn("Ignoring unrecognized/unhandled callback type: %d (%s)",
		      pkt->hdr.type, callback_name(pkt->hdr.type));
	}
	merlin_sender = NULL;
	recv_event = NULL;

	return ret;
}


static int ipc_reaper(int sd, __attribute__((unused)) int events, __attribute__((unused)) void *arg)
{
	/*
	 * this function doesn't really do anything anymore,
	 * but in case we want to handle pingbacks from the daemon,
	 * this is where they'll end up.
	 * For now we just empty the socket
	 */
	char buf[4096];
	int ret;

	do {
		ret = read(sd, buf, sizeof(buf));
		if (!ret) {
			node_disconnect(&ipc, "read() returned zero");
		} else if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			/* ignored. if this happens, the iobroker is busted */
			return 0;
		} else {
			node_disconnect(&ipc, "read() failed with error %d: %s", errno, strerror(errno));
			return 0;
		}
	} while (ret > 0);

	return 0;
}

GHashTable *host_hash_table;
node_selection *node_selection_by_hostname(const char *name)
{
	return g_hash_table_lookup(host_hash_table, name);
}

struct host_hash_table_add_parameters
{
	node_selection *sel;
	int *num_ents;
};

static gboolean host_hash_table_add_host(gpointer _name, gpointer _hst, gpointer user_data)
{
	struct host_hash_table_add_parameters *params = (struct host_hash_table_add_parameters *)user_data;
	host *hst = (host *)_hst;
	node_selection *cur = node_selection_by_hostname(hst->name);

	/*
	 * this should never happen, but if it does
	 * we just ignore it and move on
	 */
	if (cur == params->sel)
		return FALSE;

	if (cur) {
		lwarn("'%s' is checked by selection '%s', so can't add to selection '%s'",
			  hst->name, cur->name, params->sel->name);
		return FALSE;
	}
	params->num_ents[params->sel->id]++;

	g_hash_table_insert(host_hash_table, hst->name, params->sel);
	return FALSE;
}

static void setup_host_hash_tables(void)
{
	hostgroup *hg;
	int i, nsel;
	int *num_ents = NULL;

	nsel = get_num_selections();

	host_hash_table = g_hash_table_new(g_str_hash, g_str_equal);

	/*
	 * only bother if we've got hostgroups, pollers and selections.
	 * Otherwise we'll just be wasting perfectly good memory
	 * for no good reason
	 */
	if (!hostgroup_list || !num_pollers || !nsel)
		return;

	num_ents = calloc(nsel, sizeof(int));

	/*
	 * we must loop each hostgroup once, or we'll log a lot of
	 * spurious warnings that aren't exactly accurate
	 */
	for (hg = hostgroup_list; hg; hg = hg->next) {
		struct host_hash_table_add_parameters params;
		params.sel = node_selection_by_name(hg->group_name);
		params.num_ents = num_ents;

		if (!params.sel)
			continue;

		g_tree_foreach(hg->members, host_hash_table_add_host, &params);
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

		if (!strcmp(str, "all")) {
			*evt_mask = ~0;
			return ~0;
		}

		code = callback_id(str);
		if (code >= 0 && code < 32) {
			mask |= 1 << code;
		} else {
			lwarn("Unable to find a callback id for '%s'\n", str);
			return -1;
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
			lwarn("Warning: ipc_reap_interval is deprecated and no longer used");
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
		if (!strcmp(v->key, "notifies")) {
			if (!strtobool(v->value)) {
				ipc.flags &= ~(MERLIN_NODE_NOTIFIES);
			} else {
				ipc.flags |= MERLIN_NODE_NOTIFIES;
			}
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
}

static void grok_daemon_compound(struct cfg_comp *comp)
{
	uint i;

	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];
		if (!strcmp(v->key, "port")) {
			char *endp;

			default_port = (unsigned short)strtoul(v->value, &endp, 0);
			if (default_port < 1 || *endp)
				cfg_error(comp, v, "Illegal value for port: %s", v->value);
			continue;
		}
		if (!strcmp(v->key, "address")) {
			unsigned int addr;
			if (inet_pton(AF_INET, v->value, &addr) == 1)
				default_addr = addr;
			else
				cfg_error(comp, v, "Illegal value for address: %s", v->value);
			continue;
		}
	}
	for (i = 0; i < comp->nested; i++) {
		struct cfg_comp *c = comp->nest[i];
		if (!prefixcmp(c->name, "database")) {
			grok_db_compound(c);
			continue;
		}
		if (!prefixcmp(c->name, "object_config")) {
			grok_confsync_compound(c, &global_csync);
			continue;
		}
	}
}

static int read_config(char *cfg_file)
{
	uint i;
	struct cfg_comp *config;

	merlin_config_file = nspath_absolute(cfg_file, config_file_dir);

	if (!(config = cfg_parse_file(merlin_config_file))) {
		lwarn("Failed to read config file %s", merlin_config_file);
		free(merlin_config_file);
		merlin_config_file = NULL;
		return -1;
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
	/* silly fallback since we need it for expiring events */
	untracked_checks_node.data_timeout = pulse_interval * 2;
	return 0;
}


/* Nagios stuff goes below */
NEB_API_VERSION(CURRENT_NEB_API_VERSION);

void *neb_handle = NULL;

/*
 * Attempt to connect to as-yet unconnected nodes. We run this
 * every second
 */
static time_t connect_interval = 1;
static void connect_to_all(struct nm_event_execution_properties *evprop)
{
	unsigned int i;

	if (evprop->execution_type != EVENT_EXEC_NORMAL)
		return;

	schedule_event(connect_interval, connect_to_all, NULL);

	if (!ipc_is_connected(0)) {
		ipc_init();
	}

	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];
		if (node->state == STATE_CONNECTED || node->state == STATE_PENDING)
			continue;
		net_try_connect(node);
	}
}

/*
 * we send this every 15 seconds, just in case our nodes forget
 * about us. It shouldn't happen, but there are stranger things
 * than random bugs in computer programs.
 */
static void send_pulse(struct nm_event_execution_properties *evprop)
{
	unsigned int i;

	if (evprop->execution_type != EVENT_EXEC_NORMAL)
		return;

	schedule_event(pulse_interval, send_pulse, NULL);
	node_send_ctrl_active(&ipc, CTRL_GENERIC, &ipc.info);
	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = noc_table[i];
		if (node->state == STATE_CONNECTED) {
			node_send_ctrl_active(node, CTRL_GENERIC, &ipc.info);
		}
	}
}

/*
 * Sends the path to objects.cache and status.log to the
 * daemon so it can import the necessary data into the
 * database.
 */
/* check recent additions to Nagios for why these are nifty */
#define nagios_object_cache mac->x[MACRO_OBJECTCACHEFILE]
#define nagios_status_log mac->x[MACRO_STATUSDATAFILE]
/*
 * This function gets called before and after Nagios has read its config
 * and written its objects.cache and status.log files.
 * We want to setup object lists and such here, so we only care about the
 * case where config has already been read.
 */
static int post_config_init(int cb, void *ds)
{
	int result;

	if (*(int *)ds == NEBTYPE_PROCESS_START) {
		split_config();
		return 0;
	}
	if (*(int *)ds == NEBTYPE_PROCESS_EVENTLOOPSTART) {
		/* required for the 'nodeinfo' query through the query handler */
		host_check_node = calloc(num_objects.hosts, sizeof(merlin_node *));
		service_check_node = calloc(num_objects.services, sizeof(merlin_node *));
		host_expiry_map = calloc(num_objects.hosts, sizeof(timed_event *));
		service_expiry_map = calloc(num_objects.services, sizeof(timed_event *));

		/* only call this function once */
		neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, post_config_init);

		// split_config();

		linfo("Object configuration parsed.");
		if (pgroup_init() < 0)
			return -1;
		setup_host_hash_tables();
		pgroup_assign_peer_ids(ipc.pgroup);

		expired_hosts = calloc(num_objects.hosts, sizeof(void *));
		expired_services = calloc(num_objects.services, sizeof(void *));

		if((result = qh_register_handler("merlin", "Merlin information", 0, merlin_qh)) < 0)
			lerr("Failed to register query handler: %s", strerror(-result));
		else
			linfo("merlin_qh registered with query handler");

		/*
		 * it's safe to send the hash of the config we're using now that
		 * we know the local host could parse it properly.
		 * Note that this also sets up the repeating pulse-timer.
		 */
		schedule_event(0, connect_to_all, NULL);
		schedule_event(0, send_pulse, NULL);

		/*
	 	* now we register the hooks we're interested in, avoiding
	 	* the huge initial burst of events Nagios otherwise spews
	 	* at us when it's reading its status back in from the
	 	* status.sav file (assuming state retention is enabled)
	 	*/
		merlin_hooks_init(event_mask);

		if (net_init() < 0) {
			lerr("Failed to initialize networking: %s\n", strerror(errno));
			return -1;
		}

		/*
		 * this is the last event related to startup, so the regular mod hook
	 	* must see it to be able to shove startup info into the database.
	 	*/
		merlin_mod_hook(cb, ds);
	}
	return 0;
}

/*
 * This gets run when we create an ipc connection, or when that
 * connection is lost. A CTRL_ACTIVE packet should always be
 * the first to go through the ipc socket
 */
static int ipc_action_handler(merlin_node *node, int prev_state)
{
	int ret;

	ldebug("Running ipc action handler");
	if (node != &ipc || ipc.state == prev_state) {
		ldebug("  ipc_action_handler(): First exit");
		return 0;
	}

	if (ipc.state != STATE_CONNECTED) {
		/* everything is handled in node_disconnect() */
		return 0;
	}

	/* this is a connect event, so register for input */
	ret = iobroker_register(nagios_iobs, ipc.sock, (void *)&ipc, ipc_reaper);
	if (ret) {
		/* this is *really* bad */
		lerr("  ipc_action_handler(): iobroker_register(%p, %d, %p, %p) returned %d: %s",
			 nagios_iobs, ipc.sock, (void *)&ipc, ipc_reaper, ret, iobroker_strerror(ret));
	}

	/*
	 * we must use node_send_ctrl_active() here or we'll
	 * end up in an infinite loop in ipc_ctrl(), rapidly
	 * devouring all available stack space. Since we
	 * know we're connected anyways, we don't really
	 * need the ipc_is_connected(0) call that ipc_ctrl
	 * adds before trying to send.
	 */
	node_send_ctrl_active(&ipc, CTRL_GENERIC, &ipc.info);

	return 0;
}


static int node_action_handler(merlin_node *node, int prev_state)
{
	switch (node->state) {
	case STATE_CONNECTED:
		pgroup_assign_peer_ids(node->pgroup);
		break;
	case STATE_NEGOTIATING:
		node_send_ctrl_active(node, 0, &ipc.info);
		break;
	case STATE_NONE:
		memset(&node->info, 0, sizeof(node->info));
		pgroup_assign_peer_ids(node->pgroup);
		node->sock = -1;
		break;
	}

	return 0;
}

static void post_process_nodes(void)
{
	unsigned int i, x;

	ldebug("post processing %d masters, %d pollers, %d peers",
	       num_masters, num_pollers, num_peers);

	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];

		node->action = node_action_handler;
		node->expected.last_cfg_change = ipc.info.last_cfg_change;

		if (node->type == MODE_PEER) {
			memcpy(node->expected.config_hash, ipc.info.config_hash, sizeof(ipc.info.config_hash));
		}
		/* set the default push command for everything but masters */
		if (node->type != MODE_MASTER && !node->csync.configured && global_csync.push.cmd) {
			if (asprintf(&node->csync.push.cmd, "%s %s", global_csync.push.cmd, node->name) < 0)
				lerr("CSYNC: Failed to add per-node push command for %s %s: %s",
					 node_type(node), node->name, strerror(errno));
			else
				ldebug("CSYNC: Added per-node push command to %s %s as: %s",
					   node_type(node), node->name, node->csync.push.cmd);
		}
		/* set the default fetch command for all masters */
		if (node->type == MODE_MASTER && !node->csync.configured && global_csync.fetch.cmd) {
			if (asprintf(&node->csync.fetch.cmd, "%s %s", global_csync.fetch.cmd, node->name) < 0) {
				lerr("CSYNC: Failed to add per-node fetch command for %s %s: %s",
					 node_type(node), node->name, strerror(errno));
			}
			else {
				ldebug("CSYNC: Added per-node fetch command to %s %s as: %s",
					   node_type(node), node->name, node->csync.fetch.cmd);
			}
		}

		if (!node->sain.sin_port)
			node->sain.sin_port = htons(default_port);

		node->bq = nm_bufferqueue_create();
		if (node->bq == NULL) {
			lerr("Failed to create buffer queue for node %s. Aborting", node->name);
		}

		/*
		 * this lets us support multiple merlin instances on
		 * a single system, but all instances on the same
		 * system will be marked at the same time, so we skip
		 * them on the second pass here.
		 */
		if (node->flags & MERLIN_NODE_FIXED_SRCPORT) {
			continue;
		}

		if (node->sain.sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
			node->flags |= MERLIN_NODE_FIXED_SRCPORT;
			ldebug("Using fixed source-port for local %s node %s",
				   node_type(node), node->name);
			continue;
		}

		for (x = i + 1; x < num_nodes; x++) {
			merlin_node *nx = node_table[x];
			if (node->sain.sin_addr.s_addr == nx->sain.sin_addr.s_addr) {
				ldebug("Using fixed source-port for %s node %s",
				       node_type(node), node->name);
				ldebug("Using fixed source-port for %s node %s",
				       node_type(nx), nx->name);
				node->flags |= MERLIN_NODE_FIXED_SRCPORT;
				nx->flags |= MERLIN_NODE_FIXED_SRCPORT;

				if (node->sain.sin_port == nx->sain.sin_port) {
					lwarn("Nodes %s and %s have same ip *and* same port. Voodoo?",
					      node->name, nx->name);
				}
			}
		}
	}
}

/**
 * Initialization routine for the eventbroker module. This
 * function gets called by Nagios when it's done loading us
 */
int nebmodule_init(__attribute__((unused)) int flags, char *arg, nebmodule *handle)
{
	neb_handle = (void *)handle;

	self = &ipc.info;

	/*
	 * this must be zero'd out before we enter the node
	 * config parsing, or we'll clobber the values collected
	 * there and think we have no nodes configured
	 */
	memset(&ipc.info, 0, sizeof(ipc.info));

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

	/*
	 * Make sure oconfsplit is initialized. Must be before reading config
	 */
	split_init();

	if (read_config(arg) < 0) {
		nm_bufferqueue_destroy(ipc.bq);
		return -1;
	}
	log_init();


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
	ipc.info.version = MERLIN_NODEINFO_VERSION;
	ipc.info.word_size = COMPAT_WORDSIZE;
	ipc.info.byte_order = endianness();
	ipc.info.monitored_object_state_size = sizeof(monitored_object_state);
	ipc.info.object_structure_version = CURRENT_OBJECT_STRUCTURE_VERSION;
	gettimeofday(&ipc.info.start, NULL);
	ipc.info.last_cfg_change = get_last_cfg_change();
	get_config_hash(ipc.info.config_hash);

	/* make sure we can catch whatever we want */
	event_broker_options = BROKER_EVERYTHING;

	/* this gets de-registered immediately, so we need to add it manually */
	neb_register_callback(NEBCALLBACK_PROCESS_DATA, neb_handle, 0, post_config_init);

	/* only the ipc node has an action handler */
	ipc.action = ipc_action_handler;

	post_process_nodes();

	linfo("Merlin module " PACKAGE_VERSION " initialized successfully");

	return 0;
}


/**
 * Called by Nagios prior to the module being unloaded.
 * This function is supposed to release all pointers we've allocated
 * and make sure we reset it to a state where we can initialize it
 * later.
 */
int nebmodule_deinit(__attribute__((unused)) int flags, __attribute__((unused)) int reason)
{
	unsigned int i;

	linfo("Unloading Merlin module");

	ipc_deinit();
	log_deinit();
	net_deinit();

	merlin_hooks_deinit();

	/*
	 * free some readily available memory. Note that
	 * we leak some when we're being restarted through
	 * either SIGHUP or a PROGRAM_RESTART event sent to
	 * Nagios' command pipe. We also (currently) loose
	 * the ipc binlog, if any, which is slightly annoying
	 */
	nm_bufferqueue_destroy(ipc.bq);
	for (i = 0; i < num_nodes; i++) {
		struct merlin_node *node = node_table[i];
		free(node->name);
		free(node->source_name);
		free(node->hostgroups);
	}
	safe_free(node_table);

	g_hash_table_destroy(host_hash_table);

	binlog_wipe(ipc.binlog, BINLOG_UNLINK);

	pgroup_deinit();
	free(merlin_config_file);

	/*
	 * Make sure oconfsplit is uninitialized
	 */
	split_deinit();

	/*
	 * deinit logfiles last, so nothing reopens them while
	 * we're shutting down other parts
	 */
	log_deinit();

	return 0;
}
