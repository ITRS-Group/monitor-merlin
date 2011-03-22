#include <stdio.h>
#include "cfgfile.h"
#include "db_wrap.h"
#include "sql.h"
#include "slist.h"
#include "hash.h"
#include "shared.h"
#include "sha1.h"
#include "ocimp.h"
#include "nagios/objects.h"

#ifdef __GLIBC__
#include <malloc.h>
#endif

static slist *contact_slist, *host_slist, *service_slist;
static slist *sg_slist, *cg_slist, *hg_slist;
static int num_contacts = 0;
static int skip_contact_access;
static uint dodged_queries;
extern unsigned long total_queries;

struct id_tracker {
	int min, max, cur;
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

static void grok_nagios_config(const char *path)
{
}

static void grok_merlin_config(const char *path)
{
}

static void ocimp_truncate(const char *table)
{
	if (use_database) {
		sql_query("TRUNCATE TABLE %s", table);
		ldebug("Truncating table %s", table);
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
	safe_free(notes); \
	safe_free(notes_url); \
	safe_free(notification_options); \
	safe_free(output); \
	safe_free(long_output); \
	safe_free(perf_data)


static int insert_host(state_object *p)
{
	char *address;
	char *alias;
	status_prep();
	sql_quote(p->address, &address);
	sql_quote(p->alias, &alias);

	sql_query(INSERT_QUERY("host", "address, alias", "%s, %s"),
			  safe_str(address), safe_str(alias),
			  INSERT_VALUES());

	free(address);
	free(alias);
	status_free();

	return 0;
}

static int insert_service(state_object *p)
{
	char *service_description;
	status_prep();
	sql_quote(p->ido.service_description, &service_description);

	sql_query(INSERT_QUERY("service", "service_description", "%s"),
			  service_description,
			  INSERT_VALUES());

	free(service_description);
	status_free();

	return 0;
}

static inline void cfg_indent(int depth)
{
	int i;

	for (i = 0; i < depth; i++) {
		printf("  ");
	}
}

void cfg_fprintr_comp(FILE *fp, struct cfg_comp *comp, int depth)
{
	int i;

	if (!comp || !fp)
		return;

	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];
		cfg_indent(depth);
		fprintf(fp, "%s = %s\n", v->key, v->value);
	}

	for (i = 0; i < comp->nested; i++) {
		struct cfg_comp *nc = comp->nest[i];

		cfg_indent(depth);
		fprintf(fp, "%s {\n", nc->name);
		cfg_fprintr_comp(fp, nc, depth + 1);

		cfg_indent(depth);
		fprintf(fp, "}\n\n");
	}
}

int wait_for_input(void)
{
	char lbuf[4096];

	if (!isatty(fileno(stdin))) {
		return 0;
	}

	fprintf(stderr, "### AWAITING INPUT ###\n");
	fgets(lbuf, sizeof(lbuf) - 1, stdin);
	return lbuf[0];
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

static cfg_code *real_get_cfg_code(struct cfg_var *v, cfg_code *ary, int entries)
{
	int i;

	for (i = 0; i < entries; i++) {
		cfg_code *c = &ary[i];
		if (c->len == v->key_len && !memcmp(c->key, v->key, c->len))
			return c;
	}

	return NULL;
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
	comment_object cmt, *obj;
	int i = 0;
	static int truncated = 0, internal_id = 0;
	char *author = NULL, *comment_data = NULL;
	char *host_name, *service_description = NULL;

	if (!comp || !comp->vars)
		return -1;

	obj = &cmt;
	if (!truncated) {
		ocimp_truncate("comment_tbl");
		truncated = 1;
	}

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
		printf("Assumption fuckup. Get your act straight, you retard!\n");
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

static ocimp_group_object *ocimp_find_group(slist *sl, char *name)
{
	ocimp_group_object obj, *ret;
	obj.name = name;
	ret = slist_find(sl, &obj);
	if (!ret || strcmp(ret->name, obj.name))
		return NULL;
	return ret;
}

static ocimp_contact_object *ocimp_find_contact(char *name)
{
	ocimp_contact_object obj, *ret;
	obj.name = name;
	ret = slist_find(contact_slist, &obj);
	if (!ret || strcmp(ret->name, obj.name))
		return NULL;
	return ret;
}

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
			printf("slist error: bleh %s != %s\n", hst, ret->ido.host_name);
			exit(1);
		}
		if (svc && strcmp(svc, ret->ido.service_description)) {
			printf("slist error: %s != %s\n", svc, ret->ido.service_description);
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

static int parse_status(struct cfg_comp *comp)
{
	state_object *obj = NULL;
	char *host_name, *service_description = NULL;
	int located = 0, i = 0;

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
		/*
		 * some (most) of these should their id's preloaded
		 * from before. For those that don't, we generate one
		 * that we know is safe
		 */
		if (!obj->ido.id) {
			if (!service_description) {
				obj->ido.id = idt_next(&hid);
			} else {
				obj->ido.id = idt_next(&sid);
			}
		}
	}

	for (; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];
		cfg_code *ccode = get_cfg_code(v, slog_options);

		if (!ccode) {
			printf("%s: unknown variable '%s'\n", comp->name, v->key);
			continue;
		}

		switch (ccode->code) {
		case CFG_IGNORE:
			continue;

		case CFG_obsess_over_host:
		case CFG_obsess_over_service:
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
	int i;

	if (!comp)
		return -1;

	for (i = 0; i < comp->nested; i++) {
		struct cfg_comp *c = comp->nest[i];

		/*
		 * first check for the two most common types
		 */
		if (!strcmp(c->name, "servicestatus") || !strcmp(c->name, "hoststatus")) {
			parse_status(c);
			continue;
		}
		if (!strcmp(c->name, "info") || !strcmp(c->name, "programstatus")) {
			continue;
		}
		if (!strcmp(c->name, "contactstatus"))
			continue;
		if (!strcmp(c->name, "servicedowntime"))
			continue;
		if (!strcmp(c->name, "hostdowntime"))
			continue;
		if (!strcmp(c->name, "servicecomment") || !strcmp(c->name, "hostcomment")) {
			parse_comment(c);
			continue;
		}

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
	 * a comma, a space or the nul char.
	 */
	if (str[0] >= '0' && str[0] <= '9' && str[1] >= '0' && str[1] <= '9'
		&& str[2] == ':'
		&& str[3] >= '0' && str[3] <= '9' && str[4] >= '0' && str[4] <= '9'
		&& (str[5] == 0 || str[5] == ' ' || str[5] == ','))
	{
		return 1;
	}

	return 0;
}

static int handle_custom_timeperiod_var(int id, struct cfg_var *v)
{
	int i, fkey = 0;
	char *key = NULL, *value = NULL, *p;
	char *qkey, *qval;

	for (p = v->value; p && *p; p++) {
		if (is_valid_timedecl(p)) {
			value = p;
			break;
		}
	}

	if (value == v->value) {
		key = v->key;
	} else {
		/*
		 * make "v->value" hold only the parts of the
		 * variable that isn't part of the timespec.
		 */
		fkey = 1;
		value[-1] = 0;
		key = malloc(v->value_len + v->key_len + 2);
		sprintf(key, "%s %s", v->key, v->value);
		for (i = strlen(key); i; i++) {
			if (key[i] == ' ' || key[i] == '\t')
				key[i] = 0;
			else
				break;
		}
	}

	sql_quote(key, &qkey);
	sql_quote(value, &qval);

	sql_query("INSERT INTO custom_vars(obj_type, obj_id, variable, value) "
	          "VALUES (\"timeperiod\", %d, %s, %s)", id, qkey, qval);
	free(qkey);
	safe_free(qval);

	return 0;
}

static int parse_timeperiod(struct cfg_comp *comp)
{
	char *name, *alias;
	static int id = 0;
	char *sunday = NULL, *monday = NULL, *tuesday = NULL, *wednesday = NULL;
	char *thursday = NULL, *friday = NULL, *saturday = NULL;
	int i = 0;

	sql_quote(comp->vlist[i++]->value, &name);
	sql_quote(comp->vlist[i++]->value, &alias);

	id++;

	for (; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

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
		} else {
			/* custom variable */
			if (handle_custom_timeperiod_var(id, v) < 0) {
				ldebug("Unknown timeperiod variable: %s = %s\n", v->key, v->value);
			}
		}
	}

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

static void parse_group(slist *sl, struct cfg_comp *comp)
{
	ocimp_group_object *obj;

	int i = 0;
	char *name, *alias;
	char *notes = NULL, *notes_url = NULL, *action_url = NULL;
	static int id = 0;

	obj = calloc(1, sizeof(*obj));
	if (!obj) {
		lerr("Failed to calloc() %d bytes: %s", sizeof(*obj), strerror(errno));
		exit(1);
	}

	obj->id = ++id;
	obj->name = comp->vlist[i++]->value;
	sql_quote(obj->name, &name);
	sql_quote(comp->vlist[i++]->value, &alias);
	for (; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];
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

#define OCIMPT_ENTRY(type) \
	0, OCIMPT_##type, #type
static struct tbl_info {
	int truncated;
	int code;
	char *name;
	slist *sl;
} table_info[] = {
	{ OCIMPT_ENTRY(service) },
	{ OCIMPT_ENTRY(host) },
	{ OCIMPT_ENTRY(command) },
	{ OCIMPT_ENTRY(hostgroup) },
	{ OCIMPT_ENTRY(servicegroup) },
	{ OCIMPT_ENTRY(contactgroup) },
	{ OCIMPT_ENTRY(contact) },
	{ OCIMPT_ENTRY(serviceescalation) },
	{ OCIMPT_ENTRY(servicedependency) },
	{ OCIMPT_ENTRY(hostescalation) },
	{ OCIMPT_ENTRY(hostdependency) },
	{ OCIMPT_ENTRY(timeperiod) },
};

/*
 * contacts must maintain their id's (if possible) between reloads,
 * or already logged in users may change user id and get different
 * rights. This code makes it so
 */
static void preload_contact_ids(void)
{
	if (!use_database)
		return;

	cid.min = INT_MAX;

	db_wrap_result *result;
	sql_query("SELECT id, contact_name FROM contact");
	result = sql_get_result();
	if (!result) {
		lerr("Failed to preload contact id's from database");
		return;
	}

	while (result->api->step(result) == 0) {
		ocimp_contact_object *obj = NULL;
		obj = malloc(sizeof(*obj));
		result->api->get_int32_ndx(result, 0, &obj->id);
		db_wrap_result_string_copy_ndx(result, 1, &obj->name, NULL);
		slist_add(contact_slist, obj);

		idt_update(&cid, obj->id);
	}

	slist_sort(contact_slist);
}

static void parse_contact(struct cfg_comp *comp)
{
	int i = 0;
	ocimp_contact_object *obj = NULL;
	char *name = NULL;
	char *alias = NULL;
	char *service_notification_period = NULL;
	char *host_notification_period = NULL;
	char *service_notification_options = NULL;
	char *host_notification_options = NULL;
	char *service_notification_commands = NULL;
	char *host_notification_commands = NULL;
	char *email = NULL;
	char *pager = NULL;
	int host_notifications_enabled = 0;
	int service_notifications_enabled = 0;
	int can_submit_commands = 0;
	int retain_status_information;
	int retain_nonstatus_information;
	char *address1 = NULL;
	char *address2 = NULL;
	char *address3 = NULL;
	char *address4 = NULL;
	char *address5 = NULL;
	char *address6 = NULL;

	name = comp->vlist[i++]->value;
	obj = ocimp_find_contact(name);
	if (!obj) {
		/* this will happen when new contacts are added */
		obj = calloc(1, sizeof(*obj));
		if (!obj) {
			lerr("Failed to calloc() %d bytes: %s", sizeof(*obj), strerror(errno));
			exit(1);
		}

		obj->id = idt_next(&cid);
		slist_add(contact_slist, obj);
	}

	num_contacts++;

	obj->name = name;
	sql_quote(obj->name, &name);
	sql_quote(comp->vlist[i++]->value, &alias);
	sql_quote(comp->vlist[i++]->value, &service_notification_period);
	sql_quote(comp->vlist[i++]->value, &host_notification_period);
	sql_quote(comp->vlist[i++]->value, &service_notification_options);
	sql_quote(comp->vlist[i++]->value, &host_notification_options);
	sql_quote(comp->vlist[i++]->value, &service_notification_commands);
	sql_quote(comp->vlist[i++]->value, &host_notification_commands);

	for (; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];
		cfg_code *ccode;

		/* skip custom vars */
		if (*v->key == '_')
			continue;

		ccode = get_cfg_code(v, slog_options);

		if (!ccode) {
			ldebug("Unknown contact variable: %s", v->key);
			continue;
		}
		switch (ccode->code) {
		case CFG_can_submit_commands: can_submit_commands = *v->value == '1'; break;
		case CFG_host_notifications_enabled: host_notifications_enabled = *v->value == '1'; break;
		case CFG_service_notifications_enabled: service_notifications_enabled = *v->value == '1'; break;
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
			  "email, pager, address1, "
			  "address2, address3, address4, "
			  "address5, address6) "
			  "VALUES(0, %d, %s, %s, "
			  "%d, %d, "
			  "%d, "
			  "%s, %s, "
			  "%s, %s, "
			  "%s, %s, "
			  "%d, %d, "
			  "%s, %s, %s, %s, %s, "
			  "%s, %s, %s)",
			  obj->id, name, alias,
			  host_notifications_enabled, service_notifications_enabled,
			  can_submit_commands,
			  host_notification_period, service_notification_period,
			  host_notification_options, service_notification_options,
			  host_notification_commands, service_notification_commands,
			  retain_status_information, retain_nonstatus_information,
			  safe_str(email), safe_str(pager),
			  safe_str(address1), safe_str(address2), safe_str(address3),
			  safe_str(address4), safe_str(address5), safe_str(address6));

	free(name);
	free(alias);
	free(host_notification_period);
	free(service_notification_period);
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

static int parse_escalation(struct cfg_comp *comp)
{
	int i = 0, id;
	static int hostesc_id = 0, serviceesc_id = 0;
	char *hname, *sdesc = NULL;
	int first_notification, last_notification, notification_interval;
	char *escalation_period = NULL;
	char *escalation_options = NULL;
	char *cgroups;
	char *contacts;
	state_object *obj = NULL;
	const char *what, *wkey;

	hname = comp->vlist[i++]->value;
	if (*comp->name == 's') {
		sdesc = comp->vlist[i++]->value;
		wkey = what = "service";
		id = ++serviceesc_id;
	} else {
		what = "host";
		wkey = "host_name";
		id = ++hostesc_id;
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

	for (; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];
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
			  id, obj->ido.id,
			  first_notification, last_notification, notification_interval,
			  safe_str(escalation_period), safe_str(escalation_options));

	/*
	 * XXX TODO: should handle contacts and contactgroups here, as
	 * well as contact_access stuff
	 */
	safe_free(escalation_period);
	safe_free(escalation_options);
	return 0;
}

static int parse_object_cache(struct cfg_comp *comp)
{
	int i;

	if (!comp)
		return -1;

	for (i = 0; i < ARRAY_SIZE(table_info); i++) {
		struct tbl_info *table = &table_info[i];
		switch (table->code) {
		case OCIMPT_contactgroup: table->sl = cg_slist; break;
		case OCIMPT_servicegroup: table->sl = sg_slist; break;
		case OCIMPT_hostgroup: table->sl = hg_slist; break;
		}
	}

	for (i = 0; i < comp->nested; i++) {
		struct cfg_comp *c = comp->nest[i];
		int x;
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
		if (!table->truncated) {
			ocimp_truncate(table->name);
			table->truncated = 1;
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
			parse_group(table->sl, c);
			break;

		case OCIMPT_contact:
			parse_contact(c);
			break;

		case OCIMPT_hostescalation:
		case OCIMPT_serviceescalation:
			parse_escalation(c);
			break;

		default:
			lerr("Unhandled object type: %s\n", c->name);
		}
	}

	return 0;
}

static struct strvec *str_explode(char *str, int delim)
{
	int i = 0, entries = 1;
	char *p;
	struct strvec *ret = NULL;

	if (!str || !*str)
		return NULL;

	p = str;
	while ((p = strchr(p + 1, delim))) {
		entries++;
	}

	ret = malloc(sizeof(*ret));
	ret->entries = entries;
	ret->str = malloc(entries * sizeof(char *));
	ret->str[i++] = p = str;
	while ((p = strchr(p, delim))) {
		*p++ = 0;
		ret->str[i++] = p;
	}

	return ret;
}

static void fix_contacts(const char *what, state_object *o)
{
	int i;
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
		 * we cache contact access junk while we've got
		 * everything lined up properly here
		 */
		slist_add(o->contact_slist, cont);
	}
	free(contacts);
}

static void fix_contactgroups(const char *what, state_object *o)
{
	int i, x;
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
		if (!grp->strv) {
			continue;
		}

		/*
		 * now we cache contact_access stuff assigned by
		 * contact_groups. Note that fix_cg_members() must be
		 * run before this for this to work properly
		 */
		if (!grp->strv)
			continue;

		for (x = 0; x < grp->strv->entries; x++) {
			ocimp_contact_object *cont;
			cont = (ocimp_contact_object *)grp->strv->str[x];

			if (!cont) {
				lerr("Failed to locate contactgroup '%s' member '%s'",
					 grp->name, grp->strv->str[x]);
				continue;
			}

			slist_add(o->contact_slist, cont);
		}
	}

	free(cgroups);
}

static int fix_host_junctions(void *discard, void *obj)
{
	int i, host_id;
	state_object *o = (state_object *)obj;
	strvec *parents;
	char *host_name;

	host_id = o->ido.id;
	host_name = o->ido.host_name;
	parents = str_explode(o->parents, ',');

	o->contact_slist = slist_init(num_contacts, nsort_contact);
	if (!o->contact_slist) {
		lerr("Failed to initialize obj->contact_slist");
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

static int fix_service_junctions(void *discard, void *obj)
{
	state_object *o = (state_object *)obj;

	o->contact_slist = slist_init(num_contacts, nsort_contact);
	if (!o->contact_slist) {
		lerr("Failed to initialize obj->contact_slist");
	}

	fix_contacts("service", o);
	fix_contactgroups("service", o);

	return 0;
}

static int fix_cg_members(void *discard, void *base_obj)
{
	int i;
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

static int fix_hg_members(void *discard, void *base_obj)
{
	int i;
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

static int fix_sg_members(void *discard, void *base_obj)
{
	int i = 0;
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
		if (co == last_co) {
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
	ldebug("Fixing junctions");

	slist_sort(cg_slist);
	slist_sort(hg_slist);
	slist_sort(sg_slist);

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
		slist_walk(host_slist, "host", cache_contact_access);
		slist_walk(service_slist, "service", cache_contact_access);
	}

	ldebug("Done fixing junctions");
}

/*
 * This makes sure we don't overwrite instance id's when doing
 * a re-import
 */
static void load_instance_ids(void)
{
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

	sql_query("SELECT instance_id, id, host_name, service_description FROM service");
	result = sql_get_result();
	if (!result) {
		lwarn("Failed to grab service id's and instance id's from database");
		return;
	}
	while (result->api->step(result) == 0) {
		const char *service_description;
		result->api->get_string_ndx(result, 1, &host_name, &len);
		result->api->get_string_ndx(result, 2, &service_description, &len);
		obj = ocimp_find_status(host_name, service_description);

		/* service might have been deleted */
		if (!obj)
			continue;

		result->api->get_int32_ndx(result, 0, &obj->ido.instance_id);
		result->api->get_int32_ndx(result, 1, &obj->ido.id);
		idt_update(&sid, obj->ido.id);
	}

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

	printf("Usage: ocimp\n");
	exit(1);
}

int main(int argc, char **argv)
{
	struct cfg_comp *cache, *status;
	char *cache_path = "atlas/objects.cache";
	char *status_path = "atlas/status.log";
	char *nagios_cfg_path = "/opt/monitor/etc/nagios.cfg";
	char *merlin_cfg_path = "/opt/monitor/op5/merlin/merlin.conf";
	int i, use_sql = 1;
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
		if (!prefixcmp(arg, "--no-ca") || !prefixcmp(arg, "--no-contact-cache")) {
			skip_contact_access = 1;
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

		if (eq_opt) {
			opt[-1] = '=';
		}
		usage("Unknown argument: %s\n", arg);
	}

	if (merlin_cfg_path) {
		grok_merlin_config(merlin_cfg_path);
	}
	if (nagios_cfg_path) {
		grok_nagios_config(nagios_cfg_path);
	}

	log_grok_var("log_level", "all");
	log_grok_var("log_file", "stdout");
	log_init();

	use_database = use_sql;
	if (use_sql) {
		sql_config("commit_interval", "0");
		if (sql_init() < 0) {
			lerr("Failed to connect to database. Aborting");
			exit(1);
		}
	}

	/* we overallocate quite wildly for most customers' uses here */
	host_slist = slist_init(2000, alpha_cmp_host);
	service_slist = slist_init(20000, alpha_cmp_service);
	cg_slist = slist_init(100, alpha_cmp_group);
	hg_slist = slist_init(200, alpha_cmp_group);
	sg_slist = slist_init(200, alpha_cmp_group);
	contact_slist = slist_init(500, alpha_cmp_contact);
	preload_contact_ids();

	status = cfg_parse_file(status_path);
	parse_status_log(status);

	load_instance_ids();

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
	malloc_stats();
#endif
	//wait_for_input();
	return 0;
}
