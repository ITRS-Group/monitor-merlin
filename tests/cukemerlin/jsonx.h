#ifndef TESTS_CUKEMERLIN_JSONX_H_
#define TESTS_CUKEMERLIN_JSONX_H_

#include "json.h"

/* Helper to pack an array. NULL-terminate a list of JsonNodes */
JsonNode *jsonx_packarray(JsonNode *node, ...);

/* Helper to pack an object. NULL-terminate the last name and node */
JsonNode *jsonx_packobject(const char *name, JsonNode *node, ...);

/**
 * Locate a value within a JsonNode
 *
 * cmd can be one of following:
 *  'o' = next parameter is a char*, locate member in object node and recurse
 *  'a' = next parameter is an int, locate index in array node and recurse
 *  's' = next parameter is a char**, retrieve a string node and exit
 *  'i' = next parameter is an int*, retrieve a int node and exit
 *  'j' = next parameter is an JsonNode*, retrieve the current node and exit
 *
 *  Return TRUE if match is successful, FALSE if not.
 *
 *  Example:
 *
 *  node = [{"boll": "kalle"}]
 *
 *  if(jsonx_locate(node, 'a', 0, 'o', "boll", 's', &str)) {
 *      printf("Found string: %s\n", str);
 *  } else {
 *      printf("Not found\n");
 *  }
 */
int jsonx_locate(JsonNode *node, ...);

#endif
