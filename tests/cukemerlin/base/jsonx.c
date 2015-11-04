#include <stdarg.h>
#include "json.h"
#include "jsonx.h"

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
