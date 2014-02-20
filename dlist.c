#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "dlist.h"

static inline struct dlist_entry *dlist_mkentry(void *data) {
	struct dlist_entry *le;

	if ((le = calloc(1, sizeof(*le))))
		le->data = data;

	return le;
}

struct dlist_entry *dlist_create_entry(void *data) {
	return dlist_mkentry(data);
}

struct dlist_entry *dlist_insert(struct dlist_entry *list, void *data)
{
	struct dlist_entry *le;

	if (!(le = dlist_mkentry(data)))
		return NULL;

	if (list) {
		if (list->dlist_prev)
			list->dlist_prev->dlist_next = le;
		le->dlist_prev = list->dlist_prev;
		le->dlist_next = list;
		list->dlist_prev = le;
	}

	return le;
}

struct dlist_entry *dlist_append(struct dlist_entry *list, void *data)
{
	struct dlist_entry *le;

	if (!list)
		return dlist_insert(list, data);

	if (!(le = dlist_mkentry(data)))
		return NULL;

	if (list) {
		if (list->dlist_next)
			list->dlist_next->dlist_prev = le;
		le->dlist_next = list->dlist_next;
		le->dlist_prev = list;
		list->dlist_next = le;
	}

	return le;
}

void *dlist_remove(struct dlist_entry **head, struct dlist_entry *entry)
{
	void *data;

	if (!entry)
		return NULL;
	if (head && *head == entry) {
		*head = entry->dlist_prev ? entry->dlist_prev : entry->dlist_next;
	}

	if (entry->dlist_prev)
		entry->dlist_prev->dlist_next = entry->dlist_next;
	if (entry->dlist_next)
		entry->dlist_next->dlist_prev = entry->dlist_prev;

	data = entry->data;
	free(entry);

	return data;
}

struct dlist_entry *dlist_find(struct dlist_entry *entry, void *data, int (*cmp)(void *, void *, size_t), size_t size) {
	struct dlist_entry *it;

	if (!entry)
		return NULL;

	/* search forwards... */
	dlist_foreach(entry, it) {
		if (it->data == data)
			return it;
		if (it->data && data && !cmp(it->data, data, size))
			return it;
	}

	/* ... and backwards */
	dlist_foreach(entry->dlist_prev, it) {
		if (it->data == data)
			return it;
		if (it->data && data && !cmp(it->data, data, size))
			return it;
	}

	return NULL;
}

struct dlist_entry *dlist_insert_unique(struct dlist_entry *list, void *data, int (*cmp)(void *, void *, size_t), size_t size)
{
	if (dlist_find(list, data, cmp, size))
		return NULL;
	return dlist_insert(list, data);
}

struct dlist_entry *dlist_append_unique(struct dlist_entry *list, void *data, int (*cmp)(void *, void *, size_t), size_t size)
{
	if (dlist_find(list, data, cmp, size))
		return NULL;
	return dlist_append(list, data);
}

void dlist_destroy_entry(struct dlist_entry **head, struct dlist_entry *entry, void (*destructor)(void *))
{
	void *data;

	if (!entry)
		return;

	data = dlist_remove(head, entry);
	if (data && destructor)
		destructor(data);
}

void dlist_destroy_list(struct dlist_entry **list, void (*destructor)(void *))
{
	struct dlist_entry *cur, *next;

	if (!list || !*list)
		return;

	/*
	 * Hoisting the if makes this more efficient. I guess gcc
	 * optimizes poorly in the first pass due to conditionals
	 * in the _safe macros :-/
	 */
	if (!destructor) {
		dlist_foreach_safe((*list)->dlist_next, cur, next)
		free(cur);
		dlist_foreach_safe_reverse(*list, cur, next)
		free(cur);
		*list = NULL;
		return;
	}
	dlist_foreach_safe((*list)->dlist_next, cur, next) {
		destructor(cur->data);
		free(cur);
	}
	dlist_foreach_safe_reverse(*list, cur, next) {
		destructor(cur->data);
		free(cur);
	}
	*list = NULL;
}
