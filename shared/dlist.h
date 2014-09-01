#ifndef LIBNAGIOS_dlist_h__
#define LIBNAGIOS_dlist_h__

#include <errno.h>

/**
 * @file  dlist.h
 * @brief Doubly linked list (with container) API for libnagios
 *
 * This is a pretty basic doubly-linked list API. Note that we
 * have no separate list type, but deal only in entries. To get
 * a subset of a list, you can start iterating over it from
 * anywhere in the middle.
 *
 * To append entries last in the list, retain a pointer to the
 * head of it and keep issuing "dlist_append()" with the same
 * variable, like so:
@code
void *data;
struct dlist_entry *head, *entry;

head = entry = dlist_create_entry(some_data);
while ((data = get_more_data(random_resource))) {
	entry = dlist_append(entry, data);
}

dlist_foreach(head, entry) {
	do_stuff(entry->data);
}
@endcode
 *
 * To insert entries first in the list, retain a pointer to the
 * tail of it and keep issuing "dlist_insert()" with the same
 * variable, like so:
@code
void *data;
struct dlist_entry *tail, *entry;

tail = entry = dlist_create_entry(some_data);
while ((data = get_more_data(random_resource))) {
	entry = dlist_append(entry, data);
}

dlist_foreach_reverse(tail, entry) {
	do_stuff(entry->data);
}
@endcode
 *
 * Note that you can also create circular lists by appending the head
 * to the tail of the list. In that case, dlist_foreach*() macros
 * will require a break statement in order to not loop forever, and
 * dlist_destroy() will not work properly.
 *
 * @{
 */

#define DLIST_OK        0
#define DLIST_EDUPE     1
#define DLIST_ENOMEM    -ENOMEM
#define DLIST_ENOMEDIUM -ENOMEDIUM

/** dlist data entry */
struct dlist_entry {
	void *data;             /**< The data for this entry */
	struct dlist_entry *dlist_next;  /**< Next entry in the list */
	struct dlist_entry *dlist_prev;  /**< Previous entry in the list */
};
typedef struct dlist_entry dlist;

/**
 * Traverse a dlist forwards from entry__.
 * Use like so:
@code
dlist_entry *list, *cur;

dlist_foreach(list, cur) {
	data_type_in_list *data = cur->data;
	do_stuff(data);
	// calling dlist_remove(cur, (flags)); is NOT legal
}
@endcode
 *
 * @param entry__ dlist_entry: The entry to traverse from
 * @param it1__ dlist_emtry: The first iterator (may be same as entry__)
 */
#define dlist_foreach(entry__, it1__) \
	for (it1__ = entry__; it1__; it1__ = it1__->dlist_next)


/**
 * Traverse a dlist backwards from entry__
 * Use like so:
@code
dlist_entry *list, *cur;

dlist_foreach_reverse(list, cur) {
	data_type_in_list *data = cur->data;
	do_stuff(data);
	// calling dlist_remove(cur, (flags)); is NOT legal
}
@endcode
 *
 * @param entry__ dlist_entry: The entry to traverse from
 * @param it1__ dlist_emtry: The first iterator (may be same as entry__)
 */
#define dlist_foreach_reverse(entry__, it1__) \
	for (it1__ = entry__; it1__; it1__ = it1__->dlist_prev)


/**
 * Safely traverse a list forwards from entry__.
 * Use like so:
@code
dlist_entry *list, *cur, *next;
entry = do_stuff_that_creates_the_list();

dlist_foreach_safe(list, cur, next) {
	data_type_in_list *data = cur->data;
	do_stuff(data);
	free(data);
	dlist_destroy_entry(cur, DLIST_FREEDATA); // this is safe
}
@endcode
 *
 * @param entry__ dlist_entry: The entry to traverse from
 * @param it1__ dlist_emtry: The first iterator (may be same as entry__)
 * @param it2__ dlist_entry: The second iterator
 */
#define dlist_foreach_safe(entry__, it1__, it2__) \
	for (it1__ = entry__, it2__ = it1__ ? it1__->dlist_next : NULL; \
		 it1__; \
		 it1__ = it2__, it2__ = it1__ ? it1__->dlist_next : NULL)


/**
 * Safely traverse a list backwards from entry__.
 * Use like so:
@code
dlist_entry *list, *cur, *next;
list = do_stuff_that_creates_the_list();

dlist_foreach_safe(list, cur, next) {
	data_type_in_list *data = cur->data;
	do_stuff(data);
	free(data);
	dlist_destroy_entry(cur, DLIST_FREEDATA); // this is safe
}
@endcode
 *
 * @param entry__ dlist_entry: The entry to traverse from
 * @param it1__ dlist_emtry: The first iterator (may be same as entry__)
 * @param it2__ dlist_entry: The second iterator
 */
#define dlist_foreach_safe_reverse(entry__, it1__, it2__) \
	for (it1__ = entry__, it2__ = it1__ ? it1__->dlist_prev : NULL; \
		 it1__; \
		 it1__ = it2__, it2__ = it1__ ? it1__->dlist_prev : NULL)


/**
 * Create a new list (entry)
 *
 * @param data Pointer to the data to store in the list entry
 * @return An allocated dlist_entry, containing 'data'
 */
struct dlist_entry *dlist_create(void *data);

/**
 * Insert an item before **list
 * If *list is NULL, we'll create a new list.
 *
 * @param list The entry the new entry will precede
 * @param data Pointer to the data to store in the list entry
 * @return An allocated dlist_entry, containing 'data'
 */
struct dlist_entry *dlist_insert(struct dlist_entry *list, void *data);

/**
 * Append an item directly after *list
 *
 * @note This function doesn't append to the *very* end of the list,
 * but only after the current entry pointed to by *list.
 * @param list The entry the new entry will follow
 * @param data Pointer to the data to store in the list entry
 * @return An allocated dlist_entry, containing 'data'
 */
struct dlist_entry *dlist_append(struct dlist_entry *list, void *data);

/**
 * Remove an entry from the dlist (free()'ing the entry) and return
 * its data.
 * @param[ref] head The head of the list to operate on
 * @param[in] entry Entry to remove
 */
void *dlist_remove(struct dlist_entry **head, struct dlist_entry *entry);

/**
 * Locate an item preceding or following 'entry' in the list
 * @note This function does a linear scan. Use it sparingly if you're
 * interested in performance.
 *
 * @param entry Entry to start searching from
 * @param data Data to look for
 * @param cmp Comparison function (memcmp() will do a nice job)
 * @param size Size of the data we're looking for
 * @return Located dlist_entry on success; NULL on errors.
 */
struct dlist_entry *dlist_find(struct dlist_entry *entry, void *data, int (*cmp)(void *, void *, size_t), size_t size);

/**
 * Insert a unique entry into the dlist
 * @note Using this function is almost always an error, since it causes a
 * linear scan for each call.
 *
 * @param list The entry the new entry will precede
 * @param data Pointer to the data to store in the list entry
 * @param cmp The comparison function (memcmp() will do a nice job)
 * @param size The size of the data we're looking for
 * @return DLIST_OK on success; < 0 on errors.
 */
struct dlist_entry *dlist_insert_unique(struct dlist_entry *list, void *data, int (*cmp)(void *, void *, size_t), size_t size);

/**
 * Append a unique entry after the entry pointed to by *list
 *
 * @note Using this function is almost always an error, since it causes a
 * linear scan of ALL elements in the list for each call.
 * @param list The entry the new entry will precede
 * @param data Pointer to the data to store in the list entry
 * @param cmp The comparison function (memcmp() will do a nice job)
 * @param size The size of the data we're looking for
 * @return DLIST_OK on success; < 0 on errors.
 */
struct dlist_entry *dlist_append_unique(struct dlist_entry *list, void *data, int (*cmp)(void *, void *, size_t), size_t size);

/**
 * Destroy and remove a single dlist entry
 * @note You need to maintain a pointer to elsewhere in the list in
 * order to preserve it.
 * @note Don't use this to destroy lists while looping over them.
 *
 * @param[ref] head The head of the list we're operating on
 * @param entry The entry to remove
 * @param destructor Destructor to call with entry data (free() will do)
 */
void dlist_destroy_entry(struct dlist_entry **head, struct dlist_entry *entry, void (*destructor)(void *));

/**
 * Destroy the list that *list is a member of
 * @note This also sets *list to NULL
 *
 * @param list The list to destroy
 * @param destructor Destructor to call with entry data (free() will do)
 */
void dlist_destroy_list(struct dlist_entry **list, void (*destructor)(void *));

#endif /* DLIST_H */
/** @} */
