#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "slist.h"

struct sorted_list {
	int (*compare)(const void *, const void *);
	void **list;
	uint alloc, pos;
	int is_sorted;
};

void slist_sort(slist *sl)
{
	if (sl->is_sorted) {
		return;
	}

	qsort(sl->list, sl->pos, sizeof(void *), sl->compare);
	sl->is_sorted = 1;
}

/*
 * The engine of the "sorted list lookup". Arrives at the right
 * conclusion by bisecting its way around inside the sorted list.
 */
int slist_find_pos(slist *sl, const void *key)
{
	int value;
	uint high, low = 0;

	if (!sl || !sl->pos || !sl->list) {
		return -1;
	}

	high = sl->pos;
	while (high - low > 0) {
		uint mid = low + ((high - low) / 2);

		value = sl->compare(&key, &sl->list[mid]);
		if (value > 0) {
			low = mid + 1;
			continue;
		} else if (value < 0) {
			high = mid;
			continue;
		} else {
			return mid;
		}
	}

	return -1;
}

void *slist_find(slist *sl, const void *key)
{
	int slot = slist_find_pos(sl, key);

	if (slot < 0)
		return NULL;
	return sl->list[slot];
}

static int slist_grow(slist *sl, uint hint)
{
	void *ptr;

	if (!hint)
		return 0;

	ptr = realloc(sl->list, (sl->alloc + hint) * sizeof(void *));
	if (!ptr)
		return -1;
	sl->list = ptr;
	sl->alloc += hint;
	return 0;
}

int slist_push(slist *sl, void *item)
{
	if (sl->pos >= sl->alloc - 1 && slist_grow(sl, 5) < 0) {
		return -1;
	}

	sl->list[sl->pos++] = item;
	sl->is_sorted = 0;
	return 0;
}

void *slist_pop(slist *sl)
{
	void *item;

	if (!sl->pos)
		return NULL;
	sl->pos--;
	item = sl->list[sl->pos];
	sl->list[sl->pos] = NULL;
	return item;
}

slist *slist_init(uint hint, int (*cmp)(const void *, const void *))
{
	slist *sl;

	sl = calloc(1, sizeof(*sl));
	if (!sl)
		return NULL;
	if (hint)
		slist_grow(sl, hint);

	sl->compare = cmp;

	return sl;
}

int slist_set_list(slist *sl, void **list, uint items, int sorted)
{
	if (!sl || !list || !items)
		return -1;

	sl->list = list;
	sl->pos = items;
	sl->alloc = 0;
	if (!sorted) {
		slist_sort(sl);
	}
	return 0;
}

void slist_release(slist *sl)
{
	if (!sl)
		return;

	if (sl->list)
		free(sl->list);
	sl->list = NULL;
}

void slist_walk(slist *sl, void *arg, int (*cb)(void *, void *))
{
	int i;

	if (!sl || !sl->list || !sl->pos)
		return;

	for (i = 0; i < sl->pos; i++) {
		cb(arg, sl->list[i]);
	}
}
