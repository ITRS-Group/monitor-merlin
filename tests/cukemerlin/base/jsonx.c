#include <stdarg.h>
#include "json.h"
#include "jsonx.h"
#include <stdlib.h>
#include <stdio.h>

/* Helper to pack an array. NULL-terminate a list of JsonNodes */
JsonNode *jsonx_packarray(JsonNode *node, ...) {
	va_list ap;
	JsonNode *cur;
	JsonNode *result;

	result = json_mkarray();

	va_start(ap, node);
	cur = node;
	while(cur) {
		json_append_element(result, cur);
		cur = va_arg(ap, JsonNode *);
	}
	va_end(ap);

	return result;
}

/* Helper to pack an object. NULL-terminate the last name and node */
JsonNode *jsonx_packobject(const char *name, JsonNode *node, ...) {
	va_list ap;

	const char *cur_name;
	JsonNode *cur_node;

	JsonNode *result;

	result = json_mkobject();

	va_start(ap, node);
	cur_name = name;
	cur_node = node;
	while(cur_name) {
		json_append_member(result, cur_name, cur_node);
		cur_name = va_arg(ap, const char *);
		cur_node = va_arg(ap, JsonNode *);
	}
	va_end(ap);

	return result;
}

JsonNode *jsonx_clone(const JsonNode *node) {
	/* Not the most elegant implementation, but really simple */
	char *buf = json_encode(node);
	JsonNode *result = json_decode(buf);
	free(buf);
	return result;
}

JsonNode *jsonx_table_hashes(const JsonNode *node) {
	JsonNode *result = NULL;
	JsonNode *var_names;
	JsonNode *cur_row;

	/* If not a table, fail out */
	if(node->tag != JSON_ARRAY)
		goto fail_out;

	/* We need a row for var names */
	var_names = json_first_child(node);
	if(var_names == NULL)
		goto fail_out;

	/* Only allow var names list to be an array */
	if(var_names->tag != JSON_ARRAY)
		goto fail_out;

	/* Create the result array to store objects in */
	result = json_mkarray();

	/* We start on the row after var_names, and iterate */
	for( cur_row = var_names->next; cur_row != NULL; cur_row = cur_row->next ) {
		JsonNode *new_obj;
		JsonNode *cur_name;
		JsonNode *cur_val;

		/* Create object, and attach it directly, so we can fail_out any time */
		new_obj = json_mkobject();
		json_append_element(result, new_obj);

		/* Only allow array rows */
		if(cur_row->tag != JSON_ARRAY)
			goto fail_out;

		/* Start traverse in both at the same time */
		cur_name = json_first_child(var_names);
		cur_val = json_first_child(cur_row);
		while(cur_name != NULL && cur_val != NULL) {
			/* names can only be strings */
			if(cur_name->tag != JSON_STRING)
				goto fail_out;

			json_append_member(new_obj, cur_name->string_, jsonx_clone(cur_val));

			/* Jump to next */
			cur_name = cur_name->next;
			cur_val = cur_val->next;
		}

	}

	return result;

	fail_out: /* Cleanup and return NULL */
	json_delete(result);
	return NULL;
}

int jsonx_locate(JsonNode *node, ...) {
	JsonNode *cur_node = node;
	va_list ap;
	int found = 0;
	char cmd;

	va_start(ap, node);
	for(;;) {
		cmd = va_arg(ap, int);
		switch(cmd) {
		case 'o':
			{
				const char *member;
				member = va_arg(ap, const char*);
				cur_node = json_find_member(cur_node, member);
				if(cur_node == NULL)
					goto end_traverse;
			}
			break;
		case 'a':
			{
				int idx;
				idx = va_arg(ap, int);
				cur_node = json_find_element(cur_node, idx);
				if(cur_node == NULL)
					goto end_traverse;
			}
			break;
		case 's':
			{
				const char **outstring;
				outstring = va_arg(ap, const char **);
				if(cur_node->tag == JSON_STRING) {
					*outstring = cur_node->string_;
					found = 1;
				}
				goto end_traverse;
			}
			break;
		case 'l':
			{
				long *outlong;
				outlong = va_arg(ap, long *);
				if(cur_node->tag == JSON_NUMBER) {
					*outlong = (long)(cur_node->number_+0.5);
					found = 1;
				}
				if(cur_node->tag == JSON_STRING) {
					// TODO: only if string is numeric
					*outlong = atol(cur_node->string_);
					found = 1;
				}
				goto end_traverse;
			}
			break;
		case 'j':
			{
				JsonNode **outnode;
				outnode = va_arg(ap, JsonNode **);
				*outnode = cur_node;
				found = 1;
				goto end_traverse;
			}
			break;
		}
	}
	end_traverse:
	va_end(ap);
return found;
}

JsonNode *jsonx_stringobj(const JsonNode *node) {
	switch(node->tag) {
	case JSON_BOOL:
		return json_mkstring(node->bool_ ? "true" : "false");
	case JSON_NULL:
		return json_mkstring("");
	case JSON_NUMBER:
		{
			char tmpstr[256];
			sprintf(tmpstr, "%d", (int)node->number_);
			return json_mkstring(tmpstr);
		}
	case JSON_STRING:
		return json_mkstring(node->string_);
	case JSON_ARRAY:
		{
			JsonNode *result, *cur;
			result = json_mkarray();
			json_foreach(cur, node) {
				json_append_element(result, jsonx_stringobj(cur));
			}
			return result;
		}
	case JSON_OBJECT:
		{
			JsonNode *result, *cur;
			result = json_mkobject();
			json_foreach(cur, node) {
				json_append_member(result, cur->key, jsonx_stringobj(cur));
			}
			return result;
		}
	}
	return NULL; /* Shouldn't be possible to happen */
}
