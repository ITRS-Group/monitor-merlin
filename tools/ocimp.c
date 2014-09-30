#define OCIMP 1
#include <stdio.h>
#include <sys/mman.h>
#include "cfgfile.h"
#include "db_wrap.h"
#include "sql.h"
#include "slist.h"
#include "shared.h"
#include "sha1.h"
#include "ocimp.h"
#include <naemon/naemon.h>

#ifdef __GLIBC__
#include <malloc.h>
#endif

static slist *contact_slist, *host_slist, *service_slist, *timeperiod_slist;
static slist *contact_id_slist;
static slist *sg_slist, *cg_slist, *hg_slist;
static int num_contacts;
static int ocache_unchanged, skip_contact_access;
static uint dodged_queries;
static unsigned char ocache_hash[20];
static char *cache_path, *status_path;
static int no_ca_query;

struct id_tracker {
	unsigned int min, max, cur;
};
typedef struct id_tracker id_tracker;
static id_tracker cid, hid, sid;

static inline int idt_next(id_tracker *id)
{
	/* try re-using lower contact id's first */
	if (++id->cur >= id->min)
		return ++id->max;

	return id->cur;
}

static inline void idt_update(id_tracker *id, unsigned int cur)
{
	if (cur > id->max)
		id->max = cur;
	/* can't use else here since id->min is initialized to "huge" */
	if (cur < id->min)
		id->min = cur;
}


static int nsort_contact(const void *a_, const void *b_)
{
	const ocimp_contact_object *a = *((ocimp_contact_object **)a_);
	const ocimp_contact_object *b = *((ocimp_contact_object **)b_);

	return a->id - b->id;
}

/*
 * Grab some variables from nagios.cfg
 */
static void grok_nagios_config(const char *path)
{
	unsigned int i;
	struct cfg_comp *ncfg;

	if (!path)
		return;

	ncfg = cfg_parse_file(path);
	if (!ncfg || !ncfg->vars)
		return;

	for (i = 0; i < ncfg->vars; i++) {
		struct cfg_var *v = ncfg->vlist[i];
		if (!strcmp(v->key, "status_file")) {
			status_path = strdup(v->value);
			continue;
		}
		if (!strcmp(v->key, "object_cache_file")) {
			cache_path = strdup(v->value);
			continue;
		}
	}

	cfg_destroy_compound(ncfg);
}

/*
 * Recursively make sense of config compounds from merlin.conf
 */
static void grok_merlin_compound(struct cfg_comp *comp)
{
	unsigned int i;
	struct cfg_var *v;

	if (!comp || (!comp->nested && !comp->vars))
		return;

	if (!strcmp(comp->name, "database")) {
		for (i = 0; i < comp->vars; i++) {
			v = comp->vlist[i];
			if (!prefixcmp(v->key, "commit"))
				continue;
			sql_config(v->key, v->value);
		}
		return;
	}

	/* might want to put special import parameters here later */
	if (!strcmp(comp->name, "import")) {
		return;
	}

	for (i = 0; i < comp->nested; i++) {
		grok_merlin_compound(comp->nest[i]);
	}
}

static void grok_merlin_config(const char *path)
{
	struct cfg_comp *mconf;

	if (!path)
		return;

	mconf = cfg_parse_file(path);
	grok_merlin_compound(mconf);
}

static void ocimp_truncate(const char *table)
{
	if (use_database) {
		sql_query("TRUNCATE TABLE %s", table);
		/* ldebug("Truncating table %s", table); */
	}
}

static int alpha_cmp_group(const void *a_, const void *b_)
{
	const ocimp_group_object *a = *((ocimp_group_object **)a_);
	const ocimp_group_object *b = *((ocimp_group_object **)b_);

	return strcmp(a->name, b->name);
}

static int alpha_cmp_contact(const void *a_, const void *b_)
{
	const ocimp_contact_object *a = *((ocimp_contact_object **)a_);
	const ocimp_contact_object *b = *((ocimp_contact_object **)b_);

	return strcmp(a->name, b->name);
}

static int alpha_cmp_host(const void *a_, const void *b_)
{
	const state_object *a = *((state_object **)a_);
	const state_object *b = *((state_object **)b_);

	return strcmp(a->ido.host_name, b->ido.host_name);
}

static int alpha_cmp_service(const void *a_, const void *b_)
{
	const state_object *a = *((state_object **)a_);
	const state_object *b = *((state_object **)b_);
	int ret;

	ret = alpha_cmp_host(&a, &b);
	if (!ret)
		return strcmp(a->ido.service_description, b->ido.service_description);

	return ret;
}

#define qquote(key) sql_quote(p->key, &key)
#define status_prep() \
	char *host_name; \
	char *action_url; \
	char *display_name; \
	char *stalking_options; \
	char *flap_detection_options; \
	char *icon_image; \
	char *icon_image_alt; \
	char *notes; \
	char *notes_url; \
	char *notification_options; \
	char *output; \
	char *long_output; \
	char *perf_data; \
	qquote(action_url); \
	qquote(display_name); \
	qquote(stalking_options); \
	qquote(flap_detection_options); \
	qquote(icon_image); \
	qquote(icon_image_alt); \
	qquote(notes); \
	qquote(notes_url); \
	qquote(notification_options); \
	sql_quote(p->ido.host_name, &host_name); \
	sql_quote(p->state.plugin_output, &output); \
	sql_quote(p->state.long_plugin_output, &long_output); \
	sql_quote(p->state.perf_data, &perf_data)

#define status_free() \
	free(host_name); \
	safe_free(action_url); \
	safe_free(display_name); \
	safe_free(stalking_options); \
	safe_free(flap_detection_options); \
	safe_free(icon_image); \
	safe_free(icon_image_alt); \
	safe_free(notes); \
	safe_free(notes_url); \
	safe_free(notification_options); \
	safe_free(output); \
	safe_free(long_output); \
	safe_free(perf_data)


static int insert_host(state_object *p)
{
	char *address, *alias;
	char *check_command = NULL, *check_period = NULL;
	char *notification_period = NULL;
	status_prep();
	sql_quote(p->address, &address);
	if (p->alias) {
		sql_quote(p->alias, &alias);
	} else {
		alias = host_name;
	}
	sql_quote(p->check_command, &check_command);
	sql_quote(p->check_period, &check_period);
	sql_quote(p->notification_period, &notification_period);

	sql_query(INSERT_QUERY("host", "address, alias", "%s, %s"),
			  safe_str(address), safe_str(alias),
			  INSERT_VALUES());

	free(address);
	if (alias != host_name)
		free(alias);
	status_free();

	return 0;
}

static int insert_service(state_object *p)
{
	char *service_description;
	char *check_command = NULL, *check_period = NULL;
	char *notification_period = NULL;
	status_prep();
	sql_quote(p->check_command, &check_command);
	sql_quote(p->check_period, &check_period);
	sql_quote(p->ido.service_description, &service_description);
	sql_quote(p->notification_period, &notification_period);

	sql_query(INSERT_QUERY("service", "service_description, is_volatile, "
	                       "parallelize_check, next_notification",
	                       "%s, %d, %d, %ld"),
	          service_description, p->is_volatile, p->parallelize_check,
	          p->state.next_notification,
	          INSERT_VALUES());

	free(service_description);
	status_free();

	return 0;
}

static inline void cfg_indent(FILE *fp, int depth)
{
	int i;

	for (i = 0; i < depth; i++) {
		fprintf(fp, "  ");
	}
}

static int get_file_hash(blk_SHA_CTX *ctx, const char *path)
{
	struct stat st;
	void *map;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		lerr("Failed to open(%s) for reading: %s", path, strerror(errno));
		return -1;
	}
	if (fstat(fd, &st) < 0) {
		lerr("Failed to fstat() %s: %s", path, strerror(errno));
		close(fd);
		return -1;
	}

	map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (!map) {
		close(fd);
		lerr("Failed to mmap() %ld bytes of %s: %s", st.st_size, path, strerror(errno));
		return -1;
	}
	blk_SHA1_Update(ctx, map, st.st_size);

	return 0;
}

static int cfg_code_cmp(const void *a_, const void *b_)
{
	cfg_code *a = (cfg_code *)a_;
	cfg_code *b = (cfg_code *)b_;

	if(a->len != b->len)
		return a->len - b->len;
	return strcmp(a->key, b->key);
}

static cfg_code *real_get_cfg_code(struct cfg_var *v, cfg_code *ary, int entries)
{
	cfg_code seek;

	seek.key = v->key;
	seek.len = v->key_len;
	return bsearch(&seek, ary, entries, sizeof(*ary), cfg_code_cmp);
}

#define scode_str_case(key) \
	case CFG_##key: obj->state.key = v->value; break
#define scode_int_case(key) \
	case CFG_##key: obj->state.key = atoi(v->value); break
#define scode_bool_case(key) \
	case CFG_##key: obj->state.key = *v->value - '0'; break
#define scode_dbl_case(key) \
	case CFG_##key: obj->state.key = strtod(v->value, NULL); break

#define scode_bool_case_x(key, skey) \
	case CFG_##key: obj->state.skey = *v->value - '0'; break
#define scode_dbl_case_x(key, skey) \
	case CFG_##key: obj->state.skey = strtod(v->value, NULL); break

#define ccode_str_case(key) \
	case CFG_##key: obj->key = v->value; break
#define ccode_int_case(key) \
	case CFG_##key: obj->key = atoi(v->value); break
#define ccode_bool_case(key) \
	case CFG_##key: obj->key = *v->value - '0'; break
#define ccode_dbl_case(key) \
	case CFG_##key: obj->key = strtod(v->value, NULL); break

static int parse_comment(struct cfg_comp *comp)
{
	int i = 0;
	static int internal_id = 0;
	char *author = NULL, *comment_data = NULL;
	char *host_name, *service_description = NULL;

	if (!comp || !comp->vars)
		return -1;

	/*
	 * host_name is always the first variable for status objects.
	 * service_description is always the second for service thingie
	 * objects.
	 * The rest of the fields are in fixed order, so we don't even
	 * have to parse them to their natural type.
	 */
	sql_quote(comp->vlist[i++]->value, &host_name);
	if (*comp->name == 's') {
		sql_quote(comp->vlist[i++]->value, &service_description);
	} else if (*comp->name != 'h') {
		lerr("Assumption fuckup. Get your act straight, you retard!\n");
		exit(1);
	}
	sql_quote(comp->vlist[i + 7]->value, &author);
	sql_quote(comp->vlist[i + 8]->value, &comment_data);

	sql_query("INSERT INTO comment_tbl(instance_id, id, "
			  "host_name, service_description, "
			  "entry_type, comment_id, "
			  "source, persistent, "
			  "entry_time, expires, "
			  "expire_time, "
			  "author_name, comment_data) "
			  "VALUES(0, %d, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)",
			  ++internal_id,
			  host_name, safe_str(service_description),
			  comp->vlist[i]->value, comp->vlist[i + 1]->value,
			  comp->vlist[i + 2]->value, comp->vlist[i + 3]->value,
			  comp->vlist[i + 4]->value, comp->vlist[i + 5]->value,
			  comp->vlist[i + 6]->value,
			  safe_str(author), safe_str(comment_data));

	free(host_name);
	safe_free(service_description);
	safe_free(author);
	safe_free(comment_data);
	return 0;
}

static int parse_downtime(struct cfg_comp *comp)
{
	static int internal_id = 0;
	int i = 0;
	char *author = NULL, *comment_data = NULL;
	char *host_name = NULL, *service_description = NULL;
	char *downtime_id = NULL, *entry_time = NULL, *start_time = NULL, *end_time = NULL;
	char *triggered_by = NULL, *duration = NULL;
	char fixed = 0;

	if (!comp || !comp->vars)
		return -1;

	sql_quote(comp->vlist[i++]->value, &host_name);
	if (*comp->name == 's') {
		sql_quote(comp->vlist[i++]->value, &service_description);
	}
	downtime_id = comp->vlist[i++]->value;
	for (; i < (int)comp->vars; i++) {
		struct cfg_var *kv = comp->vlist[i];
		if (!strcmp(kv->key, "entry_time"))
			entry_time = kv->value;
		else if (!strcmp(kv->key, "start_time"))
			start_time = kv->value;
		else if (!strcmp(kv->key, "end_time"))
			end_time = kv->value;
		else if (!strcmp(kv->key, "triggered_by"))
			triggered_by = kv->value;
		else if (!strcmp(kv->key, "fixed"))
			fixed = *kv->value;
		else if (!strcmp(kv->key, "duration"))
			duration = kv->value;
		else if (!strcmp(kv->key, "author"))
			sql_quote(kv->value, &author);
		else if (!strcmp(kv->key, "comment"))
			sql_quote(kv->value, &comment_data);
	}

	sql_query("INSERT INTO scheduled_downtime(instance_id, id, "
			  "host_name, service_description, "
			  "downtime_id, entry_time, "
			  "start_time, end_time, "
			  "triggered_by, fixed, "
			  "duration,"
			  "author_name, comment_data, downtime_type) "
			  "VALUES(0, %d, %s, %s, %s, %s, %s, %s, %s, %c, %s, %s, %s, %d)",
			  ++internal_id,
			  host_name, safe_str(service_description),
			  downtime_id, entry_time, start_time, end_time,
			  triggered_by, fixed, duration,
			  safe_str(author), safe_str(comment_data),
			  service_description == NULL ? 2 : 1);

	free(host_name);
	safe_free(service_description);
	safe_free(author);
	safe_free(comment_data);
	return 0;
}

static ocimp_group_object *ocimp_find_group(slist *sl, char *name)
{
	ocimp_group_object obj, *ret;
	obj.name = name;
	ret = slist_find(sl, &obj);
	if (!ret || strcmp(ret->name, obj.name))
		return NULL;
	return ret;
}

static int ocimp_timeperiod_id(char *name)
{
	ocimp_group_object *obj = ocimp_find_group(timeperiod_slist, name);

	if (!obj)
		return 0;

	return obj->id;
}

static ocimp_contact_object *ocimp_locate_contact(char *name, slist *sl)
{
	ocimp_contact_object obj, *ret;
	obj.name = name;
	ret = slist_find(sl, &obj);
	if (!ret || strcmp(ret->name, obj.name))
		return NULL;

	return ret;
}
#define ocimp_find_contact(name) ocimp_locate_contact(name, contact_slist)
#define ocimp_find_contact_id(name) ocimp_locate_contact(name, contact_id_slist)

static state_object *ocimp_find_status(const char *hst, const char *svc)
{
	slist *sl;
	state_object obj, *ret;

	obj.ido.host_name = (char *)hst;
	obj.ido.service_description = (char *)svc;
	if (!svc) {
		sl = host_slist;
	} else {
		sl = service_slist;
	}

	ret = slist_find(sl, &obj);
	if (ret) {
		if (strcmp(hst, ret->ido.host_name)) {
			lerr("slist error: bleh %s != %s\n", hst, ret->ido.host_name);
			exit(1);
		}
		if (svc && strcmp(svc, ret->ido.service_description)) {
			lerr("slist error: %s != %s\n", svc, ret->ido.service_description);
			exit(1);
		}
	}
	return ret;
}

static int ocimp_stash_status(state_object *obj)
{
	if (!obj->ido.service_description) {
		slist_add(host_slist, obj);
	} else {
		slist_add(service_slist, obj);
	}
	return 0;
}

/*
 * Insert a custom variable into its proper place. comp and v always
 * exist where we want to use this, but the name of the id variable
 * differs.
 */
#define handle_custom_var(id) \
	if (!ocache_unchanged && *v->key == '_') { \
		handle_custom_variable(comp->name, id, v); \
		continue; \
	} else if (*v->key == '_') { \
		continue; \
	} else do { \
		/* nothing */ ; \
	} while (0)

static int handle_custom_variable(const char *otype, int id, struct cfg_var *v)
{
	char *key, *value, *ot;

	if (ocache_unchanged)
		return 0;

	if (!v || !otype)
		return 0;

	if (!id) {
		lerr("handle_custom_variable(%s, 0, %s = %s) is retarded. Set the id!",
			 otype, v->key, v->value);
		return 0;
	}
	sql_quote(otype, &ot);
	sql_quote(v->key, &key);
	sql_quote(v->value, &value);

	sql_query("INSERT INTO custom_vars(obj_type, obj_id, variable, value) "
			  "VALUES(%s, %d, %s, %s)", ot, id, key, value);
	safe_free(ot);
	safe_free(key);
	safe_free(value);

	return 0;
}

static int parse_status(struct cfg_comp *comp)
{
	state_object *obj = NULL;
	char *host_name, *service_description = NULL;
	int located = 0;
	unsigned int i = 0;

	if (!comp || !comp->vars)
		return -1;

	/*
	 * host_name is always the first variable for status objects.
	 * service_description is always the second for service thingie
	 * objects.
	 * After that it varies depending on if we're parsing status.log
	 * or objects.cache.
	 * First we'll try to grab this object from storage. Since we have
	 * to parse it multiple times anyway we can't really do anything
	 * about that.
	 */
	host_name = comp->vlist[i++]->value;
	if (*comp->name == 's') {
		service_description = comp->vlist[i++]->value;
	}

	obj = ocimp_find_status(host_name, service_description);

	if (!obj) {
		obj = calloc(1, sizeof(*obj));
		if (!obj) {
			lerr("Failed to calloc(1, %d): %s", sizeof(*obj), strerror(errno));
			return -1;
		}
		obj->ido.host_name = host_name;
		obj->ido.service_description = service_description;
	} else {
		located = 1;
	}

	/*
	 * some (most) of these should get their id's preloaded
	 * from before. For those that don't, we generate one
	 * at the very end that we know is safe
	 */
	if (!obj->ido.id && located) {
		if (!service_description) {
			obj->ido.id = idt_next(&hid);
		} else {
			obj->ido.id = idt_next(&sid);
		}
	}

	for (; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];
		cfg_code *ccode;

		if (!ocache_unchanged && located && *v->key == '_') {
			handle_custom_variable(comp->name, obj->ido.id, v);
			continue;
		} else if (*v->key == '_') {
			continue;
		}

		ccode = get_cfg_code(v, slog_options);
		if (!ccode) {
			printf("%s: unknown variable '%s'\n", comp->name, v->key);
			continue;
		}

		if (located) {
			/* these values are set in status.log. every field in status.log is
			 * written unconditionally, so this should be safe.
			 * */
			switch (ccode->code) {
			/* duplicated host vars */
			case CFG_check_command:
			case CFG_notification_period:
			case CFG_initial_state:
			case CFG_check_interval:
			case CFG_retry_interval:
			case CFG_max_check_attempts:
			case CFG_active_checks_enabled:
			case CFG_passive_checks_enabled:
			case CFG_obsess:
			case CFG_event_handler_enabled:
			case CFG_flap_detection_enabled:
			case CFG_notifications_enabled:
			case CFG_process_perf_data:
			case CFG_failure_prediction_enabled:
			/* extra duplicated service vars */
			case CFG_check_period:
			/* extra duplicated contact vars */
			case CFG_service_notification_period:
			case CFG_host_notification_period:
			case CFG_host_notifications_enabled:
			case CFG_service_notifications_enabled:
				continue;
			}
		}

		switch (ccode->code) {
		case CFG_IGNORE:
			continue;

		case CFG_obsess:
			obj->state.obsess = *v->value = '0';
			break;

			/* state variables, macro'd for expedience above */
			/* int vars */
			scode_int_case(last_state);
			scode_int_case(last_hard_state);
			scode_int_case(state_type);
			scode_int_case(current_attempt);
			scode_int_case(current_event_id);
			scode_int_case(current_notification_id);
			scode_int_case(current_notification_number);
			scode_int_case(current_problem_id);
			scode_int_case(last_check);
			scode_int_case(last_event_id);
			scode_int_case(last_hard_state_change);
			scode_int_case(last_notification);
			scode_int_case(last_problem_id);
			scode_int_case(last_state_change);
			scode_int_case(next_check);
			scode_int_case(next_notification);
			scode_int_case(scheduled_downtime_depth);
			scode_int_case(check_type);
			scode_int_case(current_state);
			scode_int_case(hourly_value);

			/* double vars */
			scode_dbl_case_x(check_latency, latency);
			scode_dbl_case(percent_state_change);
			scode_dbl_case_x(check_execution_time, execution_time);

			/* bool variables */
			scode_bool_case(is_flapping);
			scode_bool_case(process_performance_data);
			scode_bool_case_x(passive_checks_enabled, accept_passive_checks);
			scode_bool_case_x(active_checks_enabled, checks_enabled);
			scode_bool_case(flap_detection_enabled);
			scode_bool_case(event_handler_enabled);
			scode_bool_case(has_been_checked);
			scode_bool_case(problem_has_been_acknowledged);
			scode_bool_case(notifications_enabled);
			scode_bool_case(should_be_scheduled);
			scode_bool_case(check_freshness);

			/* string variables */
			scode_str_case(plugin_output);
			scode_str_case(long_plugin_output);


			/* from objects.cache only */

			/* strings */
			ccode_str_case(event_handler);
			ccode_str_case(notification_period);
			ccode_str_case(check_command);
			ccode_str_case(check_period);
			ccode_str_case(action_url);
			ccode_str_case(address);
			ccode_str_case(alias);
			ccode_str_case(stalking_options);
			ccode_str_case(icon_image);
			ccode_str_case(icon_image_alt);
			ccode_str_case(display_name);
			ccode_str_case(parents);
			ccode_str_case(contacts);
			ccode_str_case(contact_groups);
			ccode_str_case(notes);
			ccode_str_case(notification_options);
			ccode_str_case(flap_detection_options);
			ccode_str_case(notes_url);

			/* ints */
			ccode_int_case(retry_interval);
			ccode_int_case(check_interval);
			ccode_int_case(last_update);
			ccode_int_case(max_attempts);
			ccode_int_case(notification_interval);
			ccode_int_case(first_notification_delay);
			ccode_int_case(initial_state);

			/* bools */
			ccode_bool_case(is_volatile);
			ccode_bool_case(retain_status_information);
			ccode_bool_case(retain_nonstatus_information);
			ccode_bool_case(process_perf_data);
			ccode_bool_case(parallelize_check);

			/* doubles */
			ccode_dbl_case(low_flap_threshold);
			ccode_dbl_case(high_flap_threshold);
			ccode_dbl_case(freshness_threshold);

		case CFG_performance_data:
			obj->state.perf_data = v->value;
			break;

		default:
			printf("%s: known but unhandled variable: '%s'\n", comp->name, v->key);
		}
	}

	if (!located) {
		ocimp_stash_status(obj);
	} else {
		/* we've parsed it twice, so insert it */
		if (!service_description)
			insert_host(obj);
		else
			insert_service(obj);
	}

	return 0;
}

static int parse_status_log(struct cfg_comp *comp)
{
	unsigned int i;

	if (!comp)
		return -1;

	linfo("Parsing %s with %d compounds", status_path, comp->nested);
	if (!comp->nested)
		return -1;

	/*
	 * these always get truncated, since we must
	 * wipe them completely if there are no objects of
	 * these types in the config
	 */
	ocimp_truncate("comment_tbl");
	ocimp_truncate("scheduled_downtime");

	for (i = 0; i < comp->nested; i++) {
		struct cfg_comp *c = comp->nest[i];

		/* check for types in roughly estimated order of commonality */
		if (!strcmp(c->name, "servicestatus") || !strcmp(c->name, "hoststatus")) {
			parse_status(c);
			continue;
		}
		if (!strcmp(c->name, "servicecomment") || !strcmp(c->name, "hostcomment")) {
			parse_comment(c);
			continue;
		}
		if (!strcmp(c->name, "servicedowntime") || !strcmp(c->name, "hostdowntime")) {
			parse_downtime(c);
			continue;
		}
		if (!strcmp(c->name, "info") || !strcmp(c->name, "programstatus")) {
			continue;
		}
		if (!strcmp(c->name, "contactstatus"))
			continue;

		printf("Unhandled status.log compound type: %s\n", c->name);
	}

	return 0;
}

static int is_valid_timedecl(const char *str)
{
	if (!str || !*str)
		return 0;

	/*
	 * a valid nagios time declaration is identical to that given
	 * by a digital watch with minute precision, followed by either
	 * a comma, a space, a dash, or the nul char.
	 */
	if (str[0] >= '0' && str[0] <= '9' && str[1] >= '0' && str[1] <= '9'
		&& str[2] == ':'
		&& str[3] >= '0' && str[3] <= '9' && str[4] >= '0' && str[4] <= '9'
		&& (str[5] == 0 || str[5] == ' ' || str[5] == ',' || str[5] == '-'))
	{
		return 1;
	}

	return 0;
}

/*
 * Make a half-educated guess at what constitutes the variable
 * and what constitutes the key. timeperiods are decidedly
 * bizarre when configured from Nagios.
 */
static void resplit_timeperiod_decl(char **key, char **value)
{
	int i;
	char *new_key, *new_value = NULL, *p;

	for (p = *value; p && *p; p++) {
		if (is_valid_timedecl(p)) {
			new_value = p;
			break;
		}
	}

	if (new_value == *value)
		return;

	/*
	 * make "v->value" hold only the parts of the
	 * variable that isn't part of the timespec.
	 */
	if (new_value)
		new_value[-1] = 0;
	new_key = malloc(strlen(*key) + strlen(*value) + 2);
	sprintf(new_key, "%s %s", *key, *value);
	for (i = strlen(new_key); i; i++) {
		if (new_key[i] == ' ' || new_key[i] == '\t')
			new_key[i] = 0;
		else
			break;
	}
	*key = new_key;
	*value = new_value;
}

/*
 * Shove anything not-super-standard into custom_vars
 */
static int handle_custom_timeperiod_var(int id, struct cfg_var *v)
{
	char *qkey, *qval;

	sql_quote(v->key, &qkey);
	sql_quote(v->value, &qval);

	sql_query("INSERT INTO custom_vars(obj_type, obj_id, variable, value) "
	          "VALUES (\"timeperiod\", %d, %s, %s)", id, qkey, qval);
	free(qkey);
	safe_free(qval);

	return 0;
}

static int parse_timeperiod(struct cfg_comp *comp)
{
	ocimp_group_object *obj; /* we reuse these objects */
	char *name, *alias;
	static int id = 0;
	char *sunday = NULL, *monday = NULL, *tuesday = NULL, *wednesday = NULL;
	char *thursday = NULL, *friday = NULL, *saturday = NULL;
	unsigned int i = 0;

	obj = calloc(1, sizeof(*obj));
	if (!obj) {
		lerr("Failed to malloc(%d) bytes for timeperiod", sizeof(*obj));
		return -1;
	}

	obj->name = comp->vlist[i++]->value;
	sql_quote(obj->name, &name);
	sql_quote(comp->vlist[i++]->value, &alias);

	obj->id = ++id;

	for (; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		resplit_timeperiod_decl(&(v->key), &(v->value));

		if (!sunday && !strcmp(v->key, "sunday")) {
			sql_quote(v->value, &sunday);
		} else if (!monday && !strcmp(v->key, "monday")) {
			sql_quote(v->value, &monday);
		} else if (!tuesday && !strcmp(v->key, "tuesday")) {
			sql_quote(v->value, &tuesday);
		} else if (!wednesday && !strcmp(v->key, "wednesday")) {
			sql_quote(v->value, &wednesday);
		} else if (!thursday && !strcmp(v->key, "thursday")) {
			sql_quote(v->value, &thursday);
		} else if (!friday && !strcmp(v->key, "friday")) {
			sql_quote(v->value, &friday);
		} else if (!saturday && !strcmp(v->key, "saturday")) {
			sql_quote(v->value, &saturday);
		} else if (!obj->exclude && !strcmp(v->key, "exclude")) {
			obj->exclude = v->value;
		} else {
			/* custom variable */
			if (handle_custom_timeperiod_var(id, v) < 0) {
				ldebug("Unknown timeperiod variable: %s = %s\n", v->key, v->value);
			}
		}
	}

	slist_add(timeperiod_slist, obj);

	sql_query("INSERT INTO timeperiod(instance_id, id, "
			  "timeperiod_name, alias, "
			  "sunday, monday, tuesday, wednesday, "
			  "thursday, friday, saturday) VALUES("
			  "0, %d, %s, %s, "
			  "%s, %s, %s, %s, %s, %s, %s)",
			  id, name, alias,
			  safe_str(sunday), safe_str(monday), safe_str(tuesday),
			  safe_str(wednesday), safe_str(thursday), safe_str(friday),
			  safe_str(saturday));

	free(name);
	free(alias);
	safe_free(sunday);
	safe_free(monday);
	safe_free(tuesday);
	safe_free(wednesday);
	safe_free(thursday);
	safe_free(friday);
	safe_free(saturday);
	return 0;
}

static void parse_command(struct cfg_comp *comp)
{
	char *name, *line;
	static int id = 0;

	sql_quote(comp->vlist[0]->value, &name);
	sql_quote(comp->vlist[1]->value, &line);

	sql_query("INSERT INTO command(id, command_name, command_line) "
	          "VALUES(%d, %s, %s)", ++id, name, line);

	free(name);
	free(line);
}

static void parse_group(int *gid, slist *sl, struct cfg_comp *comp)
{
	ocimp_group_object *obj;

	unsigned int i = 0;
	char *name, *alias;
	char *notes = NULL, *notes_url = NULL, *action_url = NULL;

	obj = calloc(1, sizeof(*obj));
	if (!obj) {
		lerr("Failed to calloc() %d bytes: %s", sizeof(*obj), strerror(errno));
		exit(1);
	}

	obj->id = ++(*gid);
	obj->name = comp->vlist[i++]->value;
	sql_quote(obj->name, &name);
	sql_quote(comp->vlist[i++]->value, &alias);
	for (; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		handle_custom_var(obj->id);

		if (!strcmp(v->key, "members")) {
			obj->members = v->value;
			continue;
		}
		if (!strcmp(v->key, "notes")) {
			sql_quote(v->value, &notes);
			continue;
		}
		if (!strcmp(v->key, "notes_url")) {
			sql_quote(v->value, &notes_url);
			continue;
		}
		if (!strcmp(v->key, "action_url")) {
			sql_quote(v->value, &action_url);
			continue;
		}
	}

	if (sl == cg_slist) {
		sql_query("INSERT INTO contactgroup("
				  "instance_id, id, contactgroup_name, alias) "
				  "VALUES(0, %d, %s, %s)",
				  obj->id, name, alias);
	} else {
		char *otype;
		otype = sl == sg_slist ? "servicegroup" : "hostgroup";
		sql_query("INSERT INTO %s("
				  "instance_id, id, %s_name, alias, "
				  "notes, notes_url, action_url) "
				  "VALUES(0, %d, %s, %s, %s, %s, %s)",
				  otype, otype,
				  obj->id, name, alias,
				  safe_str(notes), safe_str(notes_url), safe_str(action_url));
	}

	slist_add(sl, obj);
	free(name);
	free(alias);
	safe_free(notes);
	safe_free(notes_url);
	safe_free(action_url);
}

#define OCIMPT_service 1
#define OCIMPT_host 2
#define OCIMPT_command 3
#define OCIMPT_hostgroup 4
#define OCIMPT_servicegroup 5
#define OCIMPT_contactgroup 6
#define OCIMPT_contact 7
#define OCIMPT_serviceescalation 8
#define OCIMPT_servicedependency 9
#define OCIMPT_hostescalation 10
#define OCIMPT_hostdependency 11
#define OCIMPT_timeperiod 12

/*
 * contacts must maintain their id's (if possible) between reloads,
 * or already logged in users may change user id and get different
 * rights. This code makes it so
 */
static void preload_contact_ids(void)
{
	int loops = 0;
	db_wrap_result *result;

	if (!use_database)
		return;

	cid.min = INT_MAX;

	linfo("Preloading contact id's from database");
	sql_query("SELECT id, contact_name FROM contact");
	result = sql_get_result();
	if (!result) {
		lerr("Failed to preload contact id's from database");
		return;
	}

	contact_id_slist = slist_init(500, alpha_cmp_contact);

	while (result->api->step(result) == 0) {
		ocimp_contact_object *obj = NULL;
		obj = malloc(sizeof(*obj));
		result->api->get_int32_ndx(result, 0, &obj->id);
		db_wrap_result_string_copy_ndx(result, 1, &obj->name, NULL);
		slist_add(contact_id_slist, obj);

		idt_update(&cid, obj->id);
		loops++;
	}
	linfo("Loaded %d contacts from database", loops);

	slist_sort(contact_id_slist);
}

static void parse_contact(struct cfg_comp *comp)
{
	unsigned int i = 0;
	ocimp_contact_object *obj = NULL;
	char *name = NULL;
	char *alias = NULL;
	int service_notification_period;
	int host_notification_period;
	char *service_notification_options = NULL;
	char *host_notification_options = NULL;
	char *service_notification_commands = NULL;
	char *host_notification_commands = NULL;
	char *email = NULL;
	char *pager = NULL;
	int host_notifications_enabled = 0;
	int service_notifications_enabled = 0;
	int can_submit_commands = 0;
	int retain_status_information = 0;
	int retain_nonstatus_information = 0;
	int minimum_value = 0;
	char *address1 = NULL;
	char *address2 = NULL;
	char *address3 = NULL;
	char *address4 = NULL;
	char *address5 = NULL;
	char *address6 = NULL;

	name = comp->vlist[i++]->value;
	obj = ocimp_find_contact_id(name);
	if (!obj) {
		/* this will happen when new contacts are added */
		obj = calloc(1, sizeof(*obj));
		if (!obj) {
			lerr("Failed to calloc() %d bytes: %s", sizeof(*obj), strerror(errno));
			exit(1);
		}

		obj->name = name;
		obj->id = idt_next(&cid);
		/* enable logins by default */
	}

	/* now add it to the proper contact list */
	obj->login_enabled = 1;
	slist_add(contact_slist, obj);

	num_contacts++;

	obj->name = name;
	sql_quote(obj->name, &name);
	sql_quote(comp->vlist[i++]->value, &alias);
	service_notification_period = ocimp_timeperiod_id(comp->vlist[i++]->value);
	host_notification_period = ocimp_timeperiod_id(comp->vlist[i++]->value);
	sql_quote(comp->vlist[i++]->value, &service_notification_options);
	sql_quote(comp->vlist[i++]->value, &host_notification_options);
	sql_quote(comp->vlist[i++]->value, &service_notification_commands);
	sql_quote(comp->vlist[i++]->value, &host_notification_commands);

	for (; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];
		cfg_code *ccode;

		if (!strcmp(v->key, "_login")) {
			obj->login_enabled = strtobool(v->value);
			continue;
		}

		handle_custom_var(obj->id);

		ccode = get_cfg_code(v, slog_options);

		if (!ccode) {
			ldebug("Unknown contact variable: %s", v->key);
			continue;
		}
		switch (ccode->code) {
		case CFG_can_submit_commands: can_submit_commands = *v->value == '1'; break;
		case CFG_host_notifications_enabled: host_notifications_enabled = *v->value == '1'; break;
		case CFG_service_notifications_enabled: service_notifications_enabled = *v->value == '1'; break;
		case CFG_minimum_value:
			minimum_value = atoi(v->value);
			break;
		case CFG_email: sql_quote(v->value, &email); break;
		case CFG_pager: sql_quote(v->value, &pager); break;
		case CFG_address1: sql_quote(v->value, &address1); break;
		case CFG_address2: sql_quote(v->value, &address2); break;
		case CFG_address3: sql_quote(v->value, &address3); break;
		case CFG_address4: sql_quote(v->value, &address4); break;
		case CFG_address5: sql_quote(v->value, &address5); break;
		case CFG_address6: sql_quote(v->value, &address6); break;
		case CFG_retain_status_information: retain_status_information = *v->value == '1'; break;
		case CFG_retain_nonstatus_information: retain_nonstatus_information = *v->value == '1'; break;

		default:
			ldebug("Unhandled contact varable: %s", v->key);
		}
	}
	sql_query("INSERT INTO contact(instance_id, id, "
			  "contact_name, alias, "
			  "host_notifications_enabled, service_notifications_enabled, "
			  "can_submit_commands, "
			  "host_notification_period, service_notification_period, "
			  "host_notification_options, service_notification_options, "
			  "host_notification_commands, service_notification_commands, "
			  "retain_status_information, retain_nonstatus_information, "
			  "email, pager, minimum_value, "
			  "address1, address2, address3, "
			  "address4, address5, address6, "
			  "last_host_notification, last_service_notification) "
			  "VALUES(0, %d, %s, %s, "
			  "%d, %d, "
			  "%d, "
			  "%d, %d, "
			  "%s, %s, "
			  "%s, %s, "
			  "%d, %d, "
			  "%s, %s, %d, "
			  "%s, %s, %s, "
			  "%s, %s, %s, "
			  "0, 0)",
			  obj->id, name, alias,
			  host_notifications_enabled, service_notifications_enabled,
			  can_submit_commands,
			  host_notification_period, service_notification_period,
			  host_notification_options, service_notification_options,
			  host_notification_commands, service_notification_commands,
			  retain_status_information, retain_nonstatus_information,
			  safe_str(email), safe_str(pager), minimum_value,
			  safe_str(address1), safe_str(address2), safe_str(address3),
			  safe_str(address4), safe_str(address5), safe_str(address6));

	free(name);
	free(alias);
	free(host_notification_options);
	free(service_notification_options);
	free(host_notification_commands);
	free(service_notification_commands);
	safe_free(email);
	safe_free(pager);
	safe_free(address1);
	safe_free(address2);
	safe_free(address3);
	safe_free(address4);
	safe_free(address5);
	safe_free(address6);
}

static int parse_escalation(int *oid, struct cfg_comp *comp)
{
	unsigned int i = 0;
	char *hname, *sdesc = NULL;
	int first_notification, last_notification, notification_interval;
	char *escalation_period = NULL;
	char *escalation_options = NULL;
	char *cgroups = NULL, *contacts = NULL;
	state_object *obj = NULL;
	const char *what, *wkey;
	strvec *sv;

	hname = comp->vlist[i++]->value;
	if (*comp->name == 's') {
		sdesc = comp->vlist[i++]->value;
		wkey = what = "service";
	} else {
		what = "host";
		wkey = "host_name";
	}

	first_notification = atoi(comp->vlist[i++]->value);
	last_notification = atoi(comp->vlist[i++]->value);
	notification_interval = atoi(comp->vlist[i++]->value);

	obj = ocimp_find_status(hname, sdesc);
	if (!obj) {
		lerr("Failed to find escalation object for %s '%s%s%s'",
			 sdesc ? "service" : "host", hname,
			 sdesc ? ";" : "", sdesc ? sdesc : "");
		return 0;
	}

	(*oid)++;
	for (; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		handle_custom_var(*oid);

		if (!strcmp(v->key, "escalation_period")) {
			sql_quote(v->value, &escalation_period);
			continue;
		}
		if (!strcmp(v->key, "escalation_options")) {
			sql_quote(v->value, &escalation_options);
			continue;
		}
		if (!strcmp(v->key, "contact_groups")) {
			cgroups = v->value;
			continue;
		}
		if (!strcmp(v->key, "contacts")) {
			contacts = v->value;
			continue;
		}
	}

	sql_query("INSERT INTO %sescalation(instance_id, id, %s, "
			  "first_notification, last_notification, notification_interval, "
			  "escalation_period, escalation_options) "
			  "VALUES(0, %d, %d, "
			  "%d, %d, %d, "
			  "%s, %s)",
			  what, wkey,
			  *oid, obj->ido.id,
			  first_notification, last_notification, notification_interval,
			  safe_str(escalation_period), safe_str(escalation_options));


	safe_free(escalation_period);
	safe_free(escalation_options);

	if (!obj->contact_slist && no_ca_query) {
		obj->contact_slist = slist_init(num_contacts, nsort_contact);
		if (!obj->contact_slist) {
			lerr("Failed to init escalation slist with %d entries: %s",
				 num_contacts, strerror(errno));
			return 0;
		}
	}

	if (contacts) {
		sv = str_explode(contacts, ',');

		for (i = 0; sv && i < sv->entries; i++) {
			ocimp_contact_object *cont = ocimp_find_contact(sv->str[i]);
			if (!cont) {
				lerr("Failed to find contact '%s' for %sescalation for %s%s%s",
					 sv->str[i], sdesc ? "service" : "host",
					 hname, sdesc ? ";" : "", sdesc ? sdesc : "");
				continue;
			}
			if (no_ca_query)
				slist_add(obj->contact_slist, cont);
			sql_query("INSERT INTO %sescalation_contact(%sescalation, contact) "
			          "VALUES(%d, %d)", what, what, *oid, cont->id);
		}

		if (sv)
			free(sv);
	}

	if (!cgroups || !(sv = str_explode(cgroups, ',')))
		return 0;

	for (i = 0; i < sv->entries; i++) {
		unsigned int x, errors = 0;
		ocimp_group_object *cg;
		strvec *members;

		cg = ocimp_find_group(cg_slist, sv->str[i]);
		if (!cg) {
			lerr("Failed to find contactgroup '%s' for %sescalation for %s%s%s",
				 sv->str[i],
				 sdesc ? "service" : "host",
				 hname, sdesc ? ";" : "", sdesc ? sdesc : "");
			continue;
		}
		sql_query("INSERT INTO %sescalation_contactgroup(%sescalation, contactgroup) "
		          "VALUES(%d, %d)", what, what, *oid, cg->id);

		if (cg->strv) {
			for (x = 0; x < cg->strv->entries; x++) {
				ocimp_contact_object *cont = (ocimp_contact_object *)cg->strv->str[x];
				if (cont->login_enabled && no_ca_query) {
					slist_add(obj->contact_slist, cont);
				}
				/*
				 * XXX escalation-hack
				 * we really shouldn't do this, but it makes the
				 * final contact_access caching query sooo much
				 * simpler.
				 */
				sql_query("INSERT INTO %sescalation_contact(%sescalation, contact) "
				          "VALUES(%d, %d)", what, what, *oid, cont->id);
			}
			continue;
		}

		if (!cg->members)
			continue;

		members = str_explode(cg->members, ',');
		for (x = 0; x < members->entries; x++) {
			ocimp_contact_object *cont = ocimp_find_contact(members->str[x]);
			if (!cont) {
				lerr("Failed to find contact '%s' as member of group %s for %sescalation for %s%s%s",
					 members->str[x], cg->name,
					 sdesc ? "service" : "host",
					 hname, sdesc ? ";" : "", sdesc ? sdesc : "");
				errors = 1;
				continue;
			}
			/*
			 * prepare for stashing contact pointers as
			 * a strv variable in the group object
			 */
			members->str[x] = (char *)cont;
			if (cont->login_enabled && no_ca_query)
				slist_add(obj->contact_slist, cont);
		}

		if (members) {
			if (!errors)
				cg->strv = members;
			else
				free(members);
		}
	}

	return 0;
}

static int parse_dependency(int *oid, struct cfg_comp *comp)
{
	unsigned int i = 0;
	state_object *obj, *dep_obj;
	char *what;
	char *hname, *dep_hname;
	char *sdesc = NULL, *dep_sdesc = NULL;
	char *dependency_period = NULL;
	char *notification_failure_options = NULL;
	char *execution_failure_options = NULL;
	int inherits_parent = '0';

	hname = comp->vlist[i++]->value;
	if (*comp->name == 's') {
		sdesc = comp->vlist[i++]->value;
		what = "service";
	} else {
		what = "host";
	}
	dep_hname = comp->vlist[i++]->value;
	if (*comp->name == 's')
		dep_sdesc = comp->vlist[i++]->value;

	obj = ocimp_find_status(hname, sdesc);
	dep_obj = ocimp_find_status(dep_hname, dep_sdesc);
	if (!obj) {
		lerr("Failed to find dependency on %s %s%s%s from %s%s%s", what,
			 hname, sdesc ? ";" : "", sdesc ? sdesc : "",
			 dep_hname, sdesc ? ";" : "", dep_sdesc ? dep_sdesc : "");
		return 0;
	}
	if (!dep_obj) {
		lerr("Failed to find dependent %s %s%s%s from %s%s%s", what,
			 dep_hname, sdesc ? ";" : "", dep_sdesc ? dep_sdesc : "",
			 hname, sdesc ? ";" : "", sdesc ? sdesc : "");
		return 0;
	}

	(*oid)++;
	for (; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		handle_custom_var(*oid);

		if (!strcmp(v->key, "dependency_period")) {
			sql_quote(v->value, &dependency_period);
			continue;
		}
		if (!strcmp(v->key, "inherits_parent")) {
			inherits_parent = *v->value;
			continue;
		}
		if (!strcmp(v->key, "execution_failure_options")) {
			sql_quote(v->value, &execution_failure_options);
			continue;
		}
		if (!strcmp(v->key, "notification_failure_options")) {
			sql_quote(v->value, &notification_failure_options);
			continue;
		}
	}

	sql_query("INSERT INTO %sdependency(instance_id, id, %s, dependent_%s, "
	          "dependency_period, inherits_parent, "
	          "execution_failure_options, notification_failure_options) "
	          "VALUES(0, %d, %d, %d, %s, %c, %s, %s)", what,
	          sdesc ? "service" : "host_name", sdesc ? "service" : "host_name",
			  *oid, obj->ido.id, dep_obj->ido.id,
	          safe_str(dependency_period), inherits_parent,
	          safe_str(execution_failure_options),
	          safe_str(notification_failure_options));

	safe_free(dependency_period);
	safe_free(execution_failure_options);
	safe_free(notification_failure_options);
	return 0;
}

#define OCIMPT_ENTRY(type, always) \
	OCIMPT_##type, always, 0, #type, NULL
static struct tbl_info {
	int code;
	int always;
	int id;
	char *name;
	slist *sl;
} table_info[] = {
	{ OCIMPT_ENTRY(service, 1) },
	{ OCIMPT_ENTRY(host, 1) },
	{ OCIMPT_ENTRY(command, 0) },
	{ OCIMPT_ENTRY(hostgroup, 0) },
	{ OCIMPT_ENTRY(servicegroup, 0) },
	{ OCIMPT_ENTRY(contactgroup, 0) },
	{ OCIMPT_ENTRY(contact, 0) },
	{ OCIMPT_ENTRY(serviceescalation, 0) },
	{ OCIMPT_ENTRY(servicedependency, 0) },
	{ OCIMPT_ENTRY(hostescalation, 0) },
	{ OCIMPT_ENTRY(hostdependency, 0) },
	{ OCIMPT_ENTRY(timeperiod, 0) },
};

static int parse_object_cache(struct cfg_comp *comp)
{
	unsigned int i;

	if (!comp)
		return -1;

	linfo("Parsing %s with %d compounds", cache_path, comp->nested);
	if (!comp->nested)
		return -1;

	/*
	 * Some tables have to be forcibly truncated to make sure
	 * no previously configured objects remain in the database
	 * in case the user has removed all of them from the config.
	 *
	 * NOTE: Services should be here too, but the chances
	 * anyone uses a Nagios system without services are
	 * quite slim, to say the least.
	 */
	if (!ocache_unchanged) {
		ocimp_truncate("timeperiod");
		ocimp_truncate("command");
		ocimp_truncate("contact");
		ocimp_truncate("contactgroup");
		ocimp_truncate("hostgroup");
		ocimp_truncate("servicegroup");
		ocimp_truncate("hostdependency");
		ocimp_truncate("hostescalation");
		ocimp_truncate("servicedependency");
		ocimp_truncate("serviceescalation");
		ocimp_truncate("host_hostgroup");
		ocimp_truncate("service_servicegroup");
		ocimp_truncate("contact_contactgroup");
		ocimp_truncate("host_contact");
		ocimp_truncate("service_contact");
		ocimp_truncate("host_contactgroup");
		ocimp_truncate("service_contactgroup");
		ocimp_truncate("hostescalation_contact");
		ocimp_truncate("hostescalation_contactgroup");
		ocimp_truncate("serviceescalation_contact");
		ocimp_truncate("serviceescalation_contactgroup");
	}

	for (i = 0; i < ARRAY_SIZE(table_info); i++) {
		struct tbl_info *table = &table_info[i];
		switch (table->code) {
		case OCIMPT_contactgroup: table->sl = cg_slist; break;
		case OCIMPT_servicegroup: table->sl = sg_slist; break;
		case OCIMPT_hostgroup: table->sl = hg_slist; break;
		}

		/*
		 * always truncate tables for objects that always
		 * get updated (only hosts and services for now)
		 */
		if (table->always)
			ocimp_truncate(table->name);
	}

	for (i = 0; i < comp->nested; i++) {
		struct cfg_comp *c = comp->nest[i];
		unsigned int x;
		struct tbl_info *table = NULL;

		/*
		 * this should always be true, but we lose very little
		 * by being anal here
		 */
		if (!prefixcmp(c->name, "define "))
			c->name += sizeof("define ") - 1;
		for (x = 0; x < ARRAY_SIZE(table_info); x++) {
			table = &table_info[x];
			if (!strcmp(c->name, table->name))
				break;
		}
		if (!table) {
			printf("Unknown (and unhandled) object type: %s\n", c->name);
			continue;
		}

		/* skip even parsing most object types if config is unchanged */
		if (!table->always && ocache_unchanged) {
			continue;
		}

		switch (table->code) {
		case OCIMPT_service: case OCIMPT_host:
			parse_status(c);
			break;

		case OCIMPT_timeperiod:
			parse_timeperiod(c);
			break;

		case OCIMPT_command:
			parse_command(c);
			break;

		case OCIMPT_contactgroup:
		case OCIMPT_hostgroup:
		case OCIMPT_servicegroup:
			parse_group(&table->id, table->sl, c);
			break;

		case OCIMPT_contact:
			parse_contact(c);
			break;

		case OCIMPT_hostescalation:
		case OCIMPT_serviceescalation:
			parse_escalation(&table->id, c);
			break;

		case OCIMPT_hostdependency:
		case OCIMPT_servicedependency:
			parse_dependency(&table->id, c);
			break;

		default:
			lerr("Unhandled object type: %s\n", c->name);
		}
	}

	return 0;
}

static void fix_contacts(const char *what, state_object *o)
{
	unsigned int i;
	struct strvec *contacts;

	if (!o || !o->contacts)
		return;

	contacts = str_explode(o->contacts, ',');

	for (i = 0; contacts && i < contacts->entries; i++) {
		struct ocimp_contact_object *cont = ocimp_find_contact(contacts->str[i]);
		if (!cont) {
			lerr("Failed to find id of contact '%s' for %s %d",
				 contacts->str[i], what, o->ido.id);
			continue;
		}
		sql_query("INSERT INTO %s_contact(%s, contact) "
				  "VALUES(%d, %d)", what, what, o->ido.id, cont->id);

		/*
		 * if we must, we cache contact access junk while we've got
		 * everything lined up properly here
		 */
		if (cont->login_enabled && no_ca_query)
			slist_add(o->contact_slist, cont);
	}
	free(contacts);
}

static void fix_contactgroups(const char *what, state_object *o)
{
	unsigned int i, x;
	struct strvec *cgroups;

	if (!o || !o->contact_groups)
		return;

	cgroups = str_explode(o->contact_groups, ',');
	for (i = 0; i < cgroups->entries; i++) {
		struct ocimp_group_object *grp = ocimp_find_group(cg_slist, cgroups->str[i]);
		if (!grp) {
			lerr("Failed to locate contactgroup '%s' for %s %d",
				 cgroups->str[i], what, o->ido.id);
			continue;
		}
		sql_query("INSERT INTO %s_contactgroup(%s, contactgroup) "
				  "VALUES(%d, %d) ", what, what, o->ido.id, grp->id);

		/*
		 * if we use old school contact_access caching we cache
		 * contacts stuff assigned by contact_groups here. Note that
		 * fix_cg_members() must be run before this for this to work
		 * properly
		 */
		if (!grp->strv || !no_ca_query) {
			continue;
		}

		for (x = 0; x < grp->strv->entries; x++) {
			ocimp_contact_object *cont;
			cont = (ocimp_contact_object *)grp->strv->str[x];

			if (!cont) {
				lerr("Failed to locate contactgroup '%s' member '%s'",
					 grp->name, grp->strv->str[x]);
				continue;
			}

			if (cont->login_enabled)
				slist_add(o->contact_slist, cont);
		}
	}

	free(cgroups);
}

static int fix_host_junctions(__attribute__((unused)) void *discard, void *obj)
{
	unsigned int i, host_id;
	state_object *o = (state_object *)obj;
	strvec *parents;
	char *host_name;

	host_id = o->ido.id;
	host_name = o->ido.host_name;
	parents = str_explode(o->parents, ',');

	/* slist might be initialized while parsing escalations */
	if (!o->contact_slist && no_ca_query) {
		o->contact_slist = slist_init(num_contacts, nsort_contact);

		if (!o->contact_slist) {
			lerr("Failed to initialize obj->contact_slist");
		}
	}

	for (i = 0; parents && i < parents->entries; i++) {
		struct state_object *parent = ocimp_find_status(parents->str[i], NULL);
		if (!parent) {
			lerr("failed to find id of parent '%s' for host '%s'",
				 parents->str[i], host_name);
			continue;
		}
		sql_query("INSERT INTO host_parents(host, parents) "
				  "VALUES(%d, %d)", host_id, parent->ido.id);
	}

	fix_contacts("host", o);
	fix_contactgroups("host", o);

	free(parents);
	return 0;
}

static int fix_service_junctions(__attribute__((unused)) void *discard, void *obj)
{
	state_object *o = (state_object *)obj;

	/* slist might be initialized while parsing escalations */
	if (!o->contact_slist) {
		o->contact_slist = slist_init(num_contacts, nsort_contact);

		if (!o->contact_slist) {
			lerr("Failed to initialize obj->contact_slist");
		}
	}

	fix_contacts("service", o);
	fix_contactgroups("service", o);

	return 0;
}

static int fix_cg_members(__attribute__((unused)) void *discard, void *base_obj)
{
	unsigned int i;
	ocimp_group_object *obj = (ocimp_group_object *)base_obj;
	strvec *strv;

	if (!obj || !obj->members)
		return 0;

	/* we might have resolved this contactgroup's members already */
	if (obj->strv)
		strv = obj->strv;
	else
		strv = str_explode(obj->members, ',');

	for (i = 0; i < strv->entries; i++) {
		ocimp_contact_object *cont;

		if (!obj->strv) {
			cont = ocimp_find_contact(strv->str[i]);
			if (!cont) {
				lerr("Failed to find contactgroup '%s' member '%s'",
					 obj->name, strv->str[i]);
				continue;
			}
			/*
			 * ugly trick, but we reuse the pointers to the contact
			 * names to stash the contact objects instead
			 */
			strv->str[i] = (char *)cont;
		} else {
			cont = (ocimp_contact_object *)strv->str[i];
		}

		if (!cont) {
			lerr("Failed to locate contact %s as member of %s",
				 strv->str[i], obj->name);
			continue;
		}
		sql_query("INSERT INTO contact_contactgroup(contact, contactgroup) "
				  "VALUES(%d, %d)", cont->id, obj->id);
	}

	obj->strv = strv;

	return 0;
}

static int fix_hg_members(__attribute__((unused)) void *discard, void *base_obj)
{
	unsigned int i;
	ocimp_group_object *obj = (ocimp_group_object *)base_obj;
	strvec *strv;

	if (!obj || !obj->members)
		return 0;

	strv = str_explode(obj->members, ',');
	for (i = 0; i < strv->entries; i++) {
		state_object *h;
		h = ocimp_find_status(strv->str[i], NULL);
		if (!h) {
			lerr("Failed to locate host '%s' as a member of '%s'",
				 strv->str[i], obj->name);
			continue;
		}
		sql_query("INSERT INTO host_hostgroup(host, hostgroup) "
				  "VALUES(%d, %d)", h->ido.id, obj->id);
	}

	free(strv);
	return 0;
}

static int fix_sg_members(__attribute__((unused)) void *discard, void *base_obj)
{
	unsigned int i = 0;
	ocimp_group_object *obj = (ocimp_group_object *)base_obj;
	strvec *strv;

	if (!obj || !obj->members)
		return 0;

	strv = str_explode(obj->members, ',');
	while (i < strv->entries - 1) {
		char *hname, *sdesc;
		state_object *s;

		hname = strv->str[i++];
		sdesc = strv->str[i++];
		s = ocimp_find_status(hname, sdesc);
		if (!s) {
			lerr("Failed to locate service '%s;%s' as a member of '%s'",
				 hname, sdesc, obj->name);
			continue;
		}
		sql_query("INSERT INTO service_servicegroup(service, servicegroup) "
				  "VALUES(%d, %d)", s->ido.id, obj->id);
	}

	free(strv);
	return 0;
}


static int fix_timeperiod_excludes(__attribute__((unused)) void *discard, void *base_obj)
{
	unsigned int i = 0;
	ocimp_group_object *obj = (ocimp_group_object *)base_obj;
	strvec *strv;

	if (!obj || !obj->exclude)
		return 0;

	strv = str_explode(obj->exclude, ',');
	if (!strv) {
		lerr("Failed to get strvec from timeperiod '%s' exclude string", obj->name);
		return 0;
	}

	for (i = 0; i < strv->entries; i++) {
		ocimp_group_object *t;

		if (!strv->str[i])
			continue;

		t = ocimp_find_group(timeperiod_slist, strv->str[i]);
		if (!t) {
			lerr("Failed to find timeperiod '%s', excluded from timeperiod '%s'",
				 strv->str[i], obj->name);
			continue;
		}
		sql_query("INSERT INTO timeperiod_exclude(timeperiod, exclude) "
		          "VALUES(%d, %d)", obj->id, t->id);
	}
	free(strv);

	return 0;
}

static void q1_cache_contact_access(void)
{
	/*
	 * someone authorized for a host is
	 * always authorized for its services
	 */
	sql_query("INSERT INTO contact_access (contact, host, service) "
			  "(SELECT contact, host, NULL FROM host_contact) UNION "
			  "(SELECT ccg.contact, hcg.host, NULL "
			      "FROM contact_contactgroup ccg "
			      "INNER JOIN host_contactgroup hcg "
			           "ON ccg.contactgroup = hcg.contactgroup) UNION "
			  "(SELECT contact, NULL, service from service_contact) UNION "
			  "(SELECT ccg.contact, NULL, scg.service "
			      "FROM contact_contactgroup ccg "
			      "INNER JOIN service_contactgroup scg "
			           "ON ccg.contactgroup = scg.contactgroup) UNION "
			  "(SELECT hc.contact, NULL, s.id "
			      "FROM host_contact hc "
			      "INNER JOIN host h "
			           "ON h.id = hc.host "
			      "INNER JOIN service s "
			           "ON h.host_name = s.host_name) UNION "
			  "(SELECT ccg.contact, NULL, service.id "
			      "FROM contact_contactgroup ccg "
			      "INNER JOIN host_contactgroup hcg "
			           "ON ccg.contactgroup = hcg.contactgroup "
			      "INNER JOIN host h ON h.id = hcg.host "
			      "INNER JOIN service ON service.host_name = h.host_name) "
			  /*
			   * contact_access through escalations relies on the
			   * quite hackish thing we do up above to put all
			   * contacts for an escalation into the
			   * {host,service}escalation_contact table. Otherwise
			   * these queries would *really* be nightmarish
			   *
			   * XXX escalation-hack
			   */
			  /* serviceescalations */
			  "UNION (SELECT sec.contact, NULL, se.service "
			      "FROM serviceescalation_contact sec, serviceescalation se "
			      "WHERE sec.serviceescalation = se.id)"
			  /*
			   * hostescalations. Contacts for any host through
			   * escalations also get access to all its services
			   */
			  "UNION (SELECT hec.contact, he.host_name, NULL "
			      "FROM hostescalation_contact hec, hostescalation he "
			      "WHERE hec.hostescalation = he.id) "
			  "UNION (SELECT hec.contact, NULL, s.id "
			      "FROM hostescalation_contact hec "
			      "INNER JOIN hostescalation he "
			           "ON hec.hostescalation = he.id "
			      "INNER JOIN host h ON h.id = he.host_name "
			      "INNER JOIN service s ON s.host_name = h.host_name)"
			 );
}

static int cache_contact_access(void *what_ptr, void *base_obj)
{
	state_object *o = (state_object *)base_obj;
	const char *what = (const char *)what_ptr;
	ocimp_contact_object **co_list, *co, *last_co = NULL;
	uint entries, i;

	if (!o->contact_slist)
		return 0;

	entries = slist_entries(o->contact_slist);
	if (!entries)
		return 0;

	slist_sort(o->contact_slist);
	co_list = (ocimp_contact_object **)slist_get_list(o->contact_slist);
	if (!co_list)
		return 0;

	for (i = 0; i < entries; i++) {
		co = co_list[i];
		if (co == last_co || !co->login_enabled) {
			dodged_queries++;
			continue;
		}
		last_co = co;
		sql_query("INSERT INTO contact_access(%s, contact) "
				  "VALUES(%d, %d)", what, o->ido.id, co->id);
	}

	return 0;
}

static void fix_junctions(void)
{
	if (ocache_unchanged)
		return;

	ldebug("Fixing junctions");

	slist_sort(cg_slist);
	slist_sort(hg_slist);
	slist_sort(sg_slist);
	slist_sort(contact_slist);
	slist_sort(timeperiod_slist);

	ocimp_truncate("timeperiod_exclude");
	slist_walk(timeperiod_slist, NULL, fix_timeperiod_excludes);

	/*
	 * fix_cg_members() loads the contactgroup objects with pointers
	 * to their member objects, so we must run that first in order
	 * to be able to populate the contact_access cache table while
	 * fiddling with the hosts and services later
	 */
	ocimp_truncate("contact_contactgroup");
	slist_walk(cg_slist, NULL, fix_cg_members);

	ocimp_truncate("host_parents");
	ocimp_truncate("host_contact");
	ocimp_truncate("host_contactgroup");
	slist_walk(host_slist, NULL, fix_host_junctions);

	ocimp_truncate("service_contact");
	ocimp_truncate("service_contactgroup");
	slist_walk(service_slist, NULL, fix_service_junctions);

	ocimp_truncate("host_hostgroup");
	slist_walk(hg_slist, NULL, fix_hg_members);

	ocimp_truncate("service_servicegroup");
	slist_walk(sg_slist, NULL, fix_sg_members);

	if (!skip_contact_access) {
		/* this is the heaviest part, really */
		ocimp_truncate("contact_access");
		/*
		 * The Query is a lot faster than caching manually, but
		 * we retain the old behaviour if people request it
		 * explicitly, even though we know it's flawed as contacts
		 * don't automatically become contacts for all services on
		 * hosts they're contacts for.
		 */
		if (no_ca_query) {
			ldebug("Using oldschool contact access caching");
			slist_walk(host_slist, "host", cache_contact_access);
			slist_walk(service_slist, "service", cache_contact_access);
		} else {
			q1_cache_contact_access();
		}
	}

	ldebug("Done fixing junctions");
}

/*
 * This makes sure we don't overwrite instance id's when doing
 * a re-import
 */
static void load_instance_ids(void)
{
	int loops = 0;
	db_wrap_result *result;
	state_object *obj;
	const char *host_name;
	size_t len;

	hid.min = sid.min = INT_MAX;
	if (!use_database)
		return;

	ldebug("Loading instance id's and id's for hosts and services");

	sql_query("SELECT instance_id, id, host_name FROM host");
	result = sql_get_result();
	if (!result) {
		lwarn("Failed to grab instance id's and id's from database");
		return;
	}
	while (result->api->step(result) == 0) {
		loops++;
		result->api->get_string_ndx(result, 2, &host_name, &len);
		obj = ocimp_find_status(host_name, NULL);

		/* this can happen normally if hosts have been removed */
		if (!obj) {
			continue;
		}

		result->api->get_int32_ndx(result, 0, &obj->ido.instance_id);
		result->api->get_int32_ndx(result, 1, &obj->ido.id);
		idt_update(&hid, obj->ido.id);
	}
	linfo("Preloaded %d host entries", loops);

	sql_query("SELECT instance_id, id, host_name, service_description FROM service");
	result = sql_get_result();
	if (!result) {
		lwarn("Failed to grab service id's and instance id's from database");
		return;
	}
	loops = 0;
	while (result->api->step(result) == 0) {
		const char *service_description;
		loops++;
		result->api->get_string_ndx(result, 2, &host_name, &len);
		result->api->get_string_ndx(result, 3, &service_description, &len);
		obj = ocimp_find_status(host_name, service_description);

		/* service might have been deleted */
		if (!obj)
			continue;

		result->api->get_int32_ndx(result, 0, &obj->ido.instance_id);
		result->api->get_int32_ndx(result, 1, &obj->ido.id);
		idt_update(&sid, obj->ido.id);
	}
	linfo("Preloaded %d service entries", loops);

	ldebug("Done loading instance id's");
}

static void usage(const char *fmt, ...)
{
	if (fmt) {
		va_list ap;
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
	}

	printf("Usage: ocimp <options>\n");
	printf("Where options can be any of the following:\n");
	printf("  --db-name      name of database to import to (def: merlin)\n");
	printf("  --db-user      database username (def: merlin)\n");
	printf("  --db-pass      database password (def: merlin)\n");
	printf("  --db-type      database type (def: mysql)\n");
	printf("  --db-port      database port to connect to (def: type dependant)\n");
	printf("  --merlin-conf  path to merlin config file (for db info)\n");
	printf("  --merlin-cfg   As above, so below\n");
	printf("  --cache        objects.cache file to import\n");
	printf("  --status-log   path to status.log file to import\n");
	printf("  --nagios-cfg   path to nagios' main config (for objects.cache and status.log\n");
	printf("\nDebug options:\n");
	printf("  --mem-stats    Print memory stats after execution\n");
	printf("  --force        force re-import even if objects.cache hasn't changed\n");
	printf("  --no-ca-query  Don't use contact_access cache query\n");
	printf("  --no-ca        Don't populate the contact_access cache\n");
	printf("  --no-contact-cache   \"Real\" name of '--no-ca'\n");

	exit(1);
}

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif
static int load_ocache_hash(const char *ocache_path)
{
	blk_SHA_CTX ctx;
	char path[PATH_MAX];
	char last_hash[40], *oc_hash;
	int ret, fd, errors = 0;

	blk_SHA1_Init(&ctx);
	/* the only critical error */
	if (get_file_hash(&ctx, ocache_path) < 0)
		return -1;
	blk_SHA1_Final(ocache_hash, &ctx);

	sprintf(path, "%s.lastimport", ocache_path);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		lerr("Failed to open %s for reading: %s", path, strerror(errno));
		errors++;
	} else {
		ret = read(fd, last_hash, sizeof(last_hash));
		if (ret != sizeof(last_hash)) {
			lerr("Read %d bytes from %s, but expected %d",
				 ret, path, sizeof(last_hash));
			errors++;
		}
		close(fd);
	}

	oc_hash = tohex(ocache_hash, 20);
	if (!errors && !memcmp(last_hash, oc_hash, sizeof(last_hash))) {
		linfo("%s is unchanged.", ocache_path);
		ocache_unchanged = 1;
	} else {
		linfo("%s has changed. Forcing complete import.", ocache_path);
		fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0660);
		if (fd < 0) {
			lerr("Failed to open %s for writing ocache hash: %s", path, strerror(errno));
		} else {
			write(fd, tohex(ocache_hash, 20), 40);
			write(fd, "\n", 1);
			close(fd);
		}
	}

	return 0;
}

/**
 * It's possible that an import is already running. Best case scenario, they'll
 * just fight for IO and it'll take twice as long. Worst case scenario, this
 * import will truncate and start to write to a table before the other import
 * is done with it, causing duplicate entries.
 */
static void make_unique_instance(int kill_previous)
{
	pid_t mypid = getpid();
	db_wrap_result *result;

	sql_query("INSERT INTO merlin_importer VALUES(%i)", mypid);
	sql_try_commit(-1);

	// wait until current process is the one with the lowest pid to avoid races
	while (1) {
		sql_query("SELECT pid FROM merlin_importer ORDER BY pid ASC");
		result = sql_get_result();
		if (!result) {
			break;
		}
		if (result->api->step(result) == 0) {
			pid_t old_pid;
			int ret;
			result->api->get_int32_ndx(result, 0, &old_pid);
			if (old_pid == mypid) {
				// lock acquired
				break;
			}
			ldebug("Ocimp already running. %s process %i", (kill_previous ? "Killing" : "Waiting for"), old_pid);

			if (kill_previous) {
				kill(old_pid, SIGTERM);
				sleep(1);
				kill(old_pid, SIGKILL);
			} else {
				int i = 0;
				ret = kill(old_pid, 0);
				// Don't wait forever - the old pid might not even be an ocimp
				// instance any longer. After 10 minutes, assume it's ok to
				// start.
				while (ret != -1 && errno != ESRCH && ++i < 600000) {
					usleep(500);
					ret = kill(old_pid, 0);
				}
			}
			sql_query("DELETE FROM merlin_importer WHERE pid=%i", old_pid);
			sql_try_commit(-1);
		} else {
			// as we inserted our own pid at the top, we should always
			// have one line in the db, but in case weird things happen, we
			// should give up rather than busy-waiting.
			break;
		}
		sql_free_result();
	}
}

static void clean_exit(int sig)
{
	sql_query("DELETE FROM merlin_importer WHERE pid=%i", getpid());
	sql_try_commit(-1);
	_exit(!!sig);
}

int main(int argc, char **argv)
{
	struct cfg_comp *cache, *status;
	char *nagios_cfg_path = NULL, *merlin_cfg_path = NULL;
	int i, use_sql = 1, force = 0, print_memory_stats = 0;
	int print_keys = 0;
	struct timeval start, stop;

	gettimeofday(&start, NULL);

	for (i = 1; i < argc; i++) {
		char *opt, *arg = argv[i];
		int eq_opt = 0;

		opt = strchr(argv[i], '=');
		if (opt) {
			*opt++ = '\0';
			eq_opt = 1;
		} else if (i < argc - 1) {
			opt = argv[i + 1];
		}

		if (!prefixcmp(arg, "-h") || !prefixcmp(arg, "--help")) {
			usage(NULL);
		}
		if (!prefixcmp(arg, "--no-sql")) {
			use_sql = 0;
			continue;
		}
		if (!strcmp(arg, "--no-ca-query")) {
			no_ca_query = 1;
			continue;
		}
		if (!prefixcmp(arg, "--no-ca") || !prefixcmp(arg, "--no-contact-cache")) {
			skip_contact_access = 1;
			continue;
		}
		if (!strcmp(arg, "--force")) {
			force = 1;
			continue;
		}
		if(!strcmp(arg, "--print-keys")) {
			print_keys = 1;
			continue;
		}

		if (!opt) {
			usage("Illegal argument, or argument without parameter: %s\n", arg);
		}
		if (!prefixcmp(arg, "--db-")) {
			char *sql_key = arg + 5;
			if (sql_config(sql_key, opt) < 0) {
				usage("Illegal database option: %s\n", arg);
			}
			continue;
		}
		if (!prefixcmp(arg, "--merlin-conf") || !prefixcmp(arg, "--merlin-cfg")) {
			merlin_cfg_path = opt;
			continue;
		}
		if (!prefixcmp(arg, "--nagios-cfg")) {
			nagios_cfg_path = opt;
			continue;
		}
		if (!prefixcmp(arg, "--cache")) {
			cache_path = opt;
			continue;
		}
		if (!prefixcmp(arg, "--status-log")) {
			status_path = opt;
			continue;
		}
		if (!prefixcmp(arg, "--mem-stats")) {
			print_memory_stats = 1;
			continue;
		}

		if (eq_opt) {
			opt[-1] = '=';
		}
		usage("Unknown argument: %s\n", arg);
	}

	qsort(slog_options, ARRAY_SIZE(slog_options), sizeof(*slog_options), cfg_code_cmp);
	if(print_keys) {
		for(i = 0; i < (int)ARRAY_SIZE(slog_options); i++) {
			printf("%s\n", slog_options[i].key);
		}
		exit(0);
	}
	if (merlin_cfg_path) {
		grok_merlin_config(merlin_cfg_path);
	}

	if (!cache_path && !status_path) {
		if (!nagios_cfg_path)
			nagios_cfg_path = "/opt/monitor/etc/nagios.cfg";

		grok_nagios_config(nagios_cfg_path);
	}

	log_grok_var("log_level", "all");
	log_grok_var("log_file", "stdout");
	log_init();

	if (load_ocache_hash(cache_path) < 0) {
		lerr("Failed to load hash from object cache file %s: %m", cache_path);
		exit(1);
	}

	use_database = use_sql;
	if (use_sql) {
		sql_config("commit_interval", "0");
		sql_config("commit_queries", "10000");
		if (sql_init() < 0) {
			lerr("Failed to connect to database. Aborting");
			exit(1);
		}
	}

	/* we overallocate quite wildly for most customers' uses here */
	host_slist = slist_init(5000, alpha_cmp_host);
	service_slist = slist_init(50000, alpha_cmp_service);
	cg_slist = slist_init(500, alpha_cmp_group);
	hg_slist = slist_init(500, alpha_cmp_group);
	sg_slist = slist_init(500, alpha_cmp_group);
	timeperiod_slist = slist_init(100, alpha_cmp_group);
	contact_slist = slist_init(500, alpha_cmp_contact);
	preload_contact_ids();

	if (force) {
		ocache_unchanged = 0;
	}

	if (use_sql) {
		// when ocache is unchanged, wait, otherwise kill
		make_unique_instance(!ocache_unchanged);
	}

	signal(SIGINT, clean_exit);
	signal(SIGTERM, clean_exit);

	if (!ocache_unchanged) {
		ocimp_truncate("custom_vars");
	}

	/*
	 * order matters greatly here. We must first parse status.log
	 * so we load all currently active hosts and services.
	 */
	if (status_path) {
		status = cfg_parse_file(status_path);
		if (!status) {
			lerr("Failed to parse '%s'", status_path);
			exit(1);
		}
		parse_status_log(status);
		linfo("Sorting host and service lists for binary search");
		slist_sort(host_slist);
		slist_sort(service_slist);
	}

	/*
	 * Now we can load the id's and instance_id's for hosts and
	 * services so we preserve the "who ran the check last" info.
	 */
	load_instance_ids();

	/*
	 * And finally we parse objects.cache. Since object and
	 * instance id's are now properly parsed, we know that
	 * all of the objects linking to other objects by id get
	 * the right value.
	 */
	cache = cfg_parse_file(cache_path);
	if (parse_object_cache(cache) < 0) {
		fprintf(stderr, "Failed to parse %s: %s\n", cache_path, strerror(errno));
		exit(1);
	}

	fix_junctions();

	/* commit the last transaction, in case we're not using autocommit */
	if (use_database) {
		sql_try_commit(-1);
	}

	gettimeofday(&stop, NULL);
	linfo("Total queries executed: %lu. Queries dodged: %u",
		  total_queries, dodged_queries);
	linfo("Import completed in %s", tv_delta(&start, &stop));

#ifdef __GLIBC__
	if (print_memory_stats)
		malloc_stats();
#endif

	clean_exit(0);

	return 0;
}
