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
