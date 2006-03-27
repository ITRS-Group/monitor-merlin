#ifndef HASH_H
#include <stdio.h>

#define HASH_BUCKETS (1 << 10)

typedef struct hash_bucket {
	const unsigned char *key;
	unsigned int val;
	struct hash_bucket *next;
} hash_bucket;

void *hash_init(void);
int hash_add(char *key, unsigned int val);
hash_bucket *hash_find(const char *key);
int hash_find_val(const char *key);
#endif /* HASH_H */
