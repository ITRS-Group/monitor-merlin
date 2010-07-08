#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h"
#include "shared.h" /* for ARRAY_SIZE() */
#include "test_utils.h"

static struct {
	char *k1, *k2;
} keys[] = {
	{ "nisse", "banan" },
	{ "foo", "bar" },
	{ "kalle", "penslar" },
	{ "hello", "world" },
	{ "test", "fnurg" },
	{ "bar", "nitfol" },
	{ "andreas", "regerar" },
};

static int removed;
static struct test_data {
	int x, i, j;
} del;

static struct test_data *ddup(int x, int i, int j)
{
	struct test_data *d;

	d = malloc(sizeof(*d));
	d->x = x;
	d->i = i;
	d->j = j;
	return d;
}

struct hash_check {
	uint entries, count, max, added, removed;
	int ent_delta, addrm_delta;
};

static int del_matching(void *data)
{
	struct test_data *d = (struct test_data *)data;

	if (!memcmp(d, &del, sizeof(del))) {
		removed++;
		return HASH_WALK_REMOVE;
	}

	return 0;
}

int main(int argc, char **argv)
{
	hash_table *ti, *tj, *tx;
	uint i, j, x;
	struct test_data s;

	t_set_colors(0);
	t_start("testing hash_walk_data()");
	memset(&s, 0, sizeof(s));
	/* first we set up the hash-tables */
	ti = hash_init(16);
	tj = hash_init(16);
	tx = hash_init(16);
	x = i = j = 0;
	for (x = 0; x < ARRAY_SIZE(keys); x++) {
		hash_add(tx, keys[x].k1, ddup(x, 0, 0));
		hash_add(tx, keys[x].k2, ddup(x, 0, 0));
		hash_add2(tx, keys[x].k1, keys[x].k2, ddup(x, 0, 0));
		s.x += 3;
		ok_int(s.x, hash_entries(tx), "x table adding");

		for (i = 0; i < ARRAY_SIZE(keys); i++) {
			hash_add(ti, keys[i].k1, ddup(x, i, 0));
			hash_add(ti, keys[i].k1, ddup(x, i, 0));
			hash_add2(ti, keys[i].k1, keys[i].k2, ddup(x, i, 0));
			s.i += 3;
			ok_int(s.i, hash_entries(ti), "i table adding");

			for (j = 0; j < ARRAY_SIZE(keys); j++) {
				hash_add(tj, keys[j].k1, ddup(x, i, j));
				hash_add(tj, keys[j].k2, ddup(x, i, j));
				hash_add2(tj, keys[j].k1, keys[j].k2, ddup(x, i, j));
				s.j += 3;
				ok_int(s.j, hash_entries(tj), "j table adding");
			}
		}
	}

	ok_int(s.x, hash_entries(tx), "x table done adding");
	ok_int(s.i, hash_entries(ti), "i table done adding");
	ok_int(s.j, hash_entries(tj), "j table done adding");

	for (x = 0; x < ARRAY_SIZE(keys); x++) {
		del.x = x;
		del.i = del.j = 0;

		ok_int(s.x, hash_entries(tx), "x table pre-delete");
		s.x -= 3;
		hash_walk_data(tx, del_matching);
		ok_int(s.x, hash_entries(tx), "x table post-delete");

		for (i = 0; i < ARRAY_SIZE(keys); i++) {
			del.i = i;
			del.j = 0;
			ok_int(s.i, hash_entries(ti), "i table pre-delete");
			hash_walk_data(ti, del_matching);
			s.i -= 3;
			ok_int(s.i, hash_entries(ti), "i table post-delete");

			for (j = 0; j < ARRAY_SIZE(keys); j++) {
				del.j = j;
				ok_int(s.j, hash_entries(tj), "j table pre-delete");
				hash_walk_data(tj, del_matching);
				s.j -= 3;
				ok_int(s.j, hash_entries(tj), "j table post-delete");
			}
		}
	}

	ok_int(0, hash_entries(tx), "x table post all ops");
	ok_int(0, hash_check_table(tx), "x table consistency post all ops");
	ok_int(0, hash_entries(ti), "i table post all ops");
	ok_int(0, hash_check_table(ti), "i table consistency post all ops");
	ok_int(0, hash_entries(tj), "j table post all ops");
	ok_int(0, hash_check_table(tj), "j table consistency post all ops");
	hash_debug_table(tx, 0);
	hash_debug_table(ti, 0);
	hash_debug_table(tj, 0);
	return t_end();
}
