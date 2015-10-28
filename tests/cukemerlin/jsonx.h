#ifndef TESTS_CUKEMERLIN_JSONX_H_
#define TESTS_CUKEMERLIN_JSONX_H_

#include "json.h"

/* Helper to pack an array. NULL-terminate a list of JsonNodes */
JsonNode *jsonx_packarray(JsonNode *node, ...);

/* Helper to pack an object. NULL-terminate the last name and node */
JsonNode *jsonx_packobject(const char *name, JsonNode *node, ...);

#endif
