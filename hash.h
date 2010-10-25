#ifndef _HASH_H_
#define _HASH_H_
#include <stdio.h>

#define HASH_WALK_REMOVE 1

struct hash_table;
typedef struct hash_table hash_table;

#define TABLE hash_table *table
extern unsigned int hash_entries_max(TABLE);
extern unsigned int hash_entries(TABLE);
extern unsigned int hash_entries_added(TABLE);
extern unsigned int hash_entries_removed(TABLE);
extern unsigned int hash_table_size(TABLE);
extern hash_table *hash_init(unsigned int buckets);
extern void *hash_find(TABLE, const char *key);
extern void *hash_find2(TABLE, const char *k1, const char *k2);
extern int hash_add(TABLE, const char *key, void *data);
extern int hash_add2(TABLE, const char *k1, const char *k2, void *data);
extern int hash_add_unique(TABLE, const char *key, void *data);
extern int hash_add_unique2(TABLE, const char *k1, const char *k2, void *data);
extern void *hash_update(TABLE, const char *key, void *data);
extern void *hash_update2(TABLE, const char *k1, const char *k2, void *data);
extern void *hash_remove(TABLE, const char *key);
extern void *hash_remove2(TABLE, const char *k1, const char *k2);
extern void hash_walk_data(TABLE, int (*walker)(void *data));
extern int hash_check_table(TABLE);
extern unsigned int hash_count_entries(TABLE);
extern void hash_debug_print_table_data(TABLE, const char *name, int force);
#define hash_debug_table(x, force) hash_debug_print_table_data((x), #x, force)
#endif /* HASH_H */
