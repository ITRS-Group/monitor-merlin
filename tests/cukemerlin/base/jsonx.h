#ifndef TESTS_CUKEMERLIN_JSONX_H_
#define TESTS_CUKEMERLIN_JSONX_H_

#include "json.h"

/* Helper to pack an array. NULL-terminate a list of JsonNodes */
JsonNode *jsonx_packarray(JsonNode *node, ...);

/* Helper to pack an object. NULL-terminate the last name and node */
JsonNode *jsonx_packobject(const char *name, JsonNode *node, ...);

/* Helper to clone an object. */
JsonNode *jsonx_clone(const JsonNode *node);

/**
 * Convert a table to hashes objects
 *
 * Having a table:
 * [
 *   ["f_a",     "f_b"],
 *   ["value_1", "value_2"],
 *   ["value_3", "value_4"]
 * ]
 *
 * Is cloned and converted to:
 * [
 *   {"f_a": "value_1", "f_b": "value_2"},
 *   {"f_a": "value_3", "f_b": "value_4"}
 * ]
 *
 * represented as JsonNode
 *
 * Return NULL if table isn't two dimensional, where every row has equal number
 * of fields, and first row don't only contain string values.
 */
JsonNode *jsonx_table_hashes(const JsonNode *node);

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


/**
 * Convert all non-object/array fields to string
 *
 * This makes comparisons easier when interacting with loosely typed languages
 *
 * It creates a clone of the object, which needs to be freed
 */
JsonNode *jsonx_stringobj(const JsonNode *node);
#endif
