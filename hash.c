#include <stdlib.h>
#include <string.h>
#include "hash.h"

#define HASH_MASK (HASH_BUCKETS - 1)
#define hash_func(k) sdbm(k)

static hash_bucket **hash_table;

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


int hash_add(char *key, unsigned int val)
{
	hash_bucket *bkt = malloc(sizeof(*bkt));
	unsigned int h = hash_func((unsigned char *)key) & HASH_MASK;

	bkt->val = val;
	bkt->key = (unsigned char *)strdup((const char *)key);

	if (!bkt->key) {
		free(bkt);
		return 0;
	}

	/* new entries go to the top of the bucket */
	bkt->next = hash_table[h];
	hash_table[h] = bkt;

	return 1;
}


hash_bucket *hash_find(const char *key)
{
	unsigned int h;
	hash_bucket *bkt;

	h = hash_func((unsigned char *)key) & HASH_MASK;
	bkt = hash_table[h];

	while (bkt && strcmp((const char *)key, (const char *)bkt->key))
		bkt = bkt->next;

	return bkt;
}


int hash_find_val(const char *key)
{
	hash_bucket *bkt = hash_find(key);

	if (!bkt)
		return -1;

	return bkt->val;
}


void *hash_init(void)
{
	hash_table = calloc(HASH_BUCKETS, sizeof(hash_bucket *));

	return hash_table;
}
