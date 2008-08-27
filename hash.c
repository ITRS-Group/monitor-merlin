#include <stdlib.h>
#include <string.h>
#include "hash.h"

#define HASH_MASK (HASH_BUCKETS - 1)
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
	size_t num_buckets;
	size_t entries;
	size_t max_entries;
};

/* struct data access functions */
size_t hash_get_max_entries(hash_table *table)
{
	return table ? table->max_entries : 0;
}

size_t hash_get_num_entries(hash_table *table)
{
	return table ? table->entries : 0;
}

size_t hash_table_size(hash_table *table)
{
	return table ? table->num_buckets : 0;
}

/*
 * polynomial conversion ignoring overflows
 *
 * Ozan Yigit's original sdbm algorithm, but without Duff's device
 * so we can use it without knowing the length of the string
 */
static inline unsigned int sdbm(const unsigned register char *k)
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

hash_table *hash_init(size_t buckets)
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
		return hash_destroy_bucket(bkt);
	}

	prev = bkt;
	for (bkt = bkt->next; bkt; bkt = bkt->next) {
		if (!strcmp(key, bkt->key)) {
			prev->next = bkt->next;
			table->entries--;
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
		return hash_destroy_bucket(bkt);
	}

	prev = bkt;
	for (bkt = bkt->next; bkt; bkt = bkt->next) {
		if (!strcmp(k1, bkt->key) && !strcmp(k2, bkt->key2)) {
			prev->next = bkt->next;
			table->entries--;
			return hash_destroy_bucket(bkt);
		}
	}

	return NULL;
}

size_t hash_count_entries(hash_table *table)
{
	int i;
	size_t count = 0;

	for (i = 0; i < table->num_buckets; i++) {
		hash_bucket *bkt;
		for (bkt = table->buckets[i]; bkt; bkt = bkt->next)
			count++;
	}

	return count;
}

int hash_check_table(hash_table *table)
{
	return hash_count_entries(table) - table->entries;
}

void hash_walk_data(hash_table *table, int (*walker)(void *))
{
	hash_bucket *bkt;
	int i;

	for (i = 0; i < table->num_buckets; i++) {
		hash_bucket *next;
		for (bkt = table->buckets[i]; bkt; bkt = next) {
			next = bkt->next;
			walker(bkt->data);
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
