#include <stdlib.h>
#include <string.h>
#include "hash.h"

#define hash_func(k) sdbm((unsigned char *)k)
#define hash_func2(k1, k2) (hash_func(k1) ^ hash_func(k2))

typedef struct hash_bucket {
	const char *key;
	const char *key2;
	void *data;
	struct hash_bucket *next;
} hash_bucket;

struct hash_table {
	hash_bucket **buckets;
	unsigned int num_buckets;
	unsigned int added, removed;
	unsigned int entries;
	unsigned int max_entries;
};

/* struct data access functions */
unsigned int hash_entries(hash_table *table)
{
	return table ? table->entries : 0;
}

unsigned int hash_entries_max(hash_table *table)
{
	return table ? table->max_entries : 0;
}

unsigned int hash_entries_added(hash_table *table)
{
	return table ? table->added : 0;
}

unsigned int hash_entries_removed(hash_table *table)
{
	return table ? table->removed : 0;
}

unsigned int hash_table_size(hash_table *table)
{
	return table ? table->num_buckets : 0;
}

void hash_debug_print_table_data(hash_table *table, const char *name, int force)
{
	int delta = hash_check_table(table);
	unsigned int count;
	if (!delta && !force)
		return;

	count = hash_count_entries(table);
	printf("debug data for hash table '%s'\n", name);
	printf("  entries: %u; counted: %u; delta: %d\n",
		   table->entries, count, delta);
	printf("  added: %u; removed: %u; delta: %d\n",
		   table->added, table->removed, table->added - table->removed);
}

/*
 * polynomial conversion ignoring overflows
 *
 * Ozan Yigit's original sdbm algorithm, but without Duff's device
 * so we can use it without knowing the length of the string
 */
static inline unsigned int sdbm(register const unsigned char *k)
{
	register unsigned int h = 0;

	while (*k)
		h = *k++ + 65599 * h;

	return h;
}

static inline int hash_add_bucket(hash_table *table, const char *k1, const char *k2, void *data, unsigned int h)
{
	hash_bucket *bkt;

	if (!(bkt = malloc(sizeof(*bkt))))
		return -1;

	h = h % table->num_buckets;

	table->added++;
	bkt->data = data;
	bkt->key = k1;
	bkt->key2 = k2;
	bkt->next = table->buckets[h];
	table->buckets[h] = bkt;

	if (++table->entries > table->max_entries)
		table->max_entries = table->entries;

	return 0;
}

int hash_add2(hash_table *table, const char *k1, const char *k2, void *data)
{
	return hash_add_bucket(table, k1, k2, data, hash_func2(k1, k2));
}

int hash_add(hash_table *table, const char *key, void *data)
{
	return hash_add_bucket(table, key, NULL, data, hash_func(key));
}

static hash_bucket *hash_get_bucket(hash_table *table, const char *key)
{
	hash_bucket *bkt;

	if (!table)
		return NULL;

	bkt = table->buckets[hash_func(key) % table->num_buckets];
	for (; bkt; bkt = bkt->next) {
		if (!strcmp(key, bkt->key))
			return bkt;
	}

	return NULL;
}

static hash_bucket *hash_get_bucket2(hash_table *table, const char *k1, const char *k2)
{
	hash_bucket *bkt;

	if (!table)
		return NULL;

	bkt = table->buckets[hash_func2(k1, k2) % table->num_buckets];
	for (; bkt; bkt = bkt->next) {
		if (!strcmp(k1, bkt->key) && !strcmp(k2, bkt->key2))
			return bkt;
	}

	return NULL;
}

void *hash_find(hash_table *table, const char *key)
{
	hash_bucket *bkt;

	bkt = hash_get_bucket(table, key);

	return bkt ? bkt->data : NULL;
}

void *hash_find2(hash_table *table, const char *k1, const char *k2)
{
	hash_bucket *bkt;

	bkt = hash_get_bucket2(table, k1, k2);

	return bkt ? bkt->data : NULL;
}

hash_table *hash_init(unsigned int buckets)
{
	hash_table *table = calloc(sizeof(hash_table), 1);

	if (table) {
		table->buckets = calloc(buckets, sizeof(hash_bucket *));
		if (table->buckets) {
			table->num_buckets = buckets;
			return table;
		}

		free(table);
	}

	return NULL;
}

void *hash_update(hash_table *table, const char *key, void *data)
{
	hash_bucket *bkt;
	void *current_data;

	bkt = hash_get_bucket(table, key);
	if (!bkt) {
		hash_add(table, key, data);
		return NULL;
	}

	current_data = bkt->data;
	bkt->data = data;
	return current_data;
}

void *hash_update2(hash_table *table, const char *key, const char *key2, void *data)
{
	hash_bucket *bkt;

	bkt = hash_get_bucket2(table, key, key2);
	if (!bkt) {
		hash_add2(table, key, key2, data);
		return NULL;
	}

	bkt->data = data;
	return NULL;
}

static inline void *hash_destroy_bucket(hash_bucket *bkt)
{
	void *data;

	data = bkt->data;
	free(bkt);
	return data;
}

void *hash_remove(hash_table *table, const char *key)
{
	unsigned int h;
	hash_bucket *bkt, *prev;

	h = hash_func(key) % table->num_buckets;

	if (!(bkt = table->buckets[h]))
		return NULL;

	if (!strcmp(key, bkt->key)) {
		table->buckets[h] = bkt->next;
		table->entries--;
		table->removed++;
		return hash_destroy_bucket(bkt);
	}

	prev = bkt;
	for (bkt = bkt->next; bkt; bkt = bkt->next) {
		if (!strcmp(key, bkt->key)) {
			prev->next = bkt->next;
			table->entries--;
			table->removed++;
			return hash_destroy_bucket(bkt);
		}
	}

	return NULL;
}

void *hash_remove2(hash_table *table, const char *k1, const char *k2)
{
	unsigned int h;
	hash_bucket *bkt, *prev;

	h = hash_func2(k1, k2) % table->num_buckets;

	if (!(bkt = table->buckets[h]))
		return NULL;

	if (!strcmp(k1, bkt->key) && !strcmp(k2, bkt->key2)) {
		table->buckets[h] = bkt->next;
		table->entries--;
		table->removed++;
		return hash_destroy_bucket(bkt);
	}

	prev = bkt;
	for (bkt = bkt->next; bkt; bkt = bkt->next) {
		if (!strcmp(k1, bkt->key) && !strcmp(k2, bkt->key2)) {
			prev->next = bkt->next;
			table->entries--;
			table->removed++;
			return hash_destroy_bucket(bkt);
		}
	}

	return NULL;
}

unsigned int hash_count_entries(hash_table *table)
{
	unsigned int i, count = 0;

	for (i = 0; i < table->num_buckets; i++) {
		hash_bucket *bkt;
		for (bkt = table->buckets[i]; bkt; bkt = bkt->next)
			count++;
	}

	return count;
}

int hash_check_table(hash_table *table)
{
	return table ? table->entries - hash_count_entries(table) : 0;
}

void hash_walk_data(hash_table *table, int (*walker)(void *))
{
	hash_bucket *bkt, *prev;
	unsigned int i;

	if (!table->entries)
		return;

	for (i = 0; i < table->num_buckets; i++) {
		int depth = 0;

		prev = table->buckets[i];
		hash_bucket *next;
		for (bkt = table->buckets[i]; bkt; bkt = next) {
			next = bkt->next;

			if (walker(bkt->data) != HASH_WALK_REMOVE) {
				/* only update prev if we don't remove current */
				prev = bkt;
				depth++;
				continue;
			}
			table->removed++;
			table->entries--;
			hash_destroy_bucket(bkt);
			prev->next = next;
			if (!depth) {
				table->buckets[i] = next;
			}
		}
	}
}

/* inserts a guaranteed unique entry to the hash-table */
int hash_add_unique(hash_table *table, const char *key, void *data)
{
	unsigned int h;
	hash_bucket *bkt;

	h = hash_func(key) % table->num_buckets;
	for (bkt = table->buckets[h]; bkt; bkt = bkt->next)
		if (!strcmp(bkt->key, key))
			return -1;

	/* it's a unique key */
	return hash_add_bucket(table, key, NULL, data, h);
}

int hash_add_unique2(hash_table *table, const char *k1, const char *k2, void *data)
{
	unsigned int h;
	hash_bucket *bkt;

	h = hash_func2(k1, k2) % table->num_buckets;
	for (bkt = table->buckets[h]; bkt; bkt = bkt->next)
		if (!strcmp(bkt->key, k1) && !strcmp(bkt->key2, k2))
			return -1;

	return hash_add_bucket(table, k1, k2, data, h);
}
