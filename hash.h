#ifndef INCLUDE_hash_h__
#define INCLUDE_hash_h__
#include <stdio.h>

#define HASH_BUCKETS (1 << 10)

struct hash_table;
typedef struct hash_table hash_table;

#define TABLE hash_table *table
extern size_t hash_get_max_entries(TABLE);
extern size_t hash_get_num_entries(TABLE);
extern size_t hash_table_size(TABLE);
extern hash_table *hash_init(size_t buckets);
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
extern void hash_remove_data(TABLE, const char *k1, const void *data);
extern void hash_remove_data2(TABLE, const char *k1, const char *k2, const void *data);
extern void hash_walk_data(TABLE, int (*walker)(void *data));
extern int hash_check_table(TABLE);
extern size_t hash_count_entries(TABLE);
#endif /* INCLUDE_hash_h__ */
