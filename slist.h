#ifndef include_slist_h__
#define include_slist_h__
#include <stdlib.h>
struct sorted_list;
typedef struct sorted_list slist;

/**
 * Locates the position of a particular entry in the sorted list.
 * @param sl The sorted list object
 * @param key The key object, as passed to the sort/compare function
 * @return The position of the slot on success, -1 on errors
 */
extern int slist_find_pos(slist *sl, const void *key);

/**
 * Locates and returns the object stashed at a particular place in
 * the sorted list
 * @param sl The sorted list
 * @param key The key object, as passed to the sort/compare function
 * @return The object of desire on success, NULL on errors
 */
extern void *slist_find(slist *sl, const void *key);

/**
 * Sorts the sorted list object, making it suitable for lookups.
 * In order to avoid checking the sorted-ness of a list when
 * doing lookups on it, the user has to tell the API when to
 * sort the lists. This is the call to make for making sure that
 * happens. This function uses qsort(3) to sort the list.
 * @param sl The unsorted sorted-list object
 */
extern void slist_sort(slist *sl);

/**
 * Adds an item to the tail of the sorted list
 * This marks the list as unsorted and forces a sort next time a
 * lookup in the list is done. slist_add(sl, item) is a macro alias
 * for this function.
 * @param sl The sorted list to add to
 * @param item The item to add
 * @return 0 on success, -1 on errors
 */
extern int slist_push(slist *sl, void *item);
#define slist_add(sl, item) slist_push(sl, item)

/**
 * Pops the latest added item from the sorted list, or the
 * last sorted item if the list is sorted.
 * @param sl The sorted list to pop from
 * @return The last item in the list on success. NULL on errors
 */
extern void *slist_pop(slist *sl);

/**
 * Initializes a sorted list object
 * @param hint Initial slot allocation
 * @param cmp Comparison function, used for sorting and finding
 *            items in the sorted list
 * @return An initialized slist object on success. NULL on errors
 */
extern slist *slist_init(uint hint, int (*cmp)(const void *, const void *));

/**
 * Set the list of items to operate on
 * Many applications will store lists of items that they pre-sort and
 * create manually with great efficiency. In those cases the application
 * can set the list they want to search using this function.
 * The list isn't copied and its items shouldn't be released, so one
 * should avoid calling slist_release() on a sorted list object where
 * the list is stack-allocated. Such lists must be free()'d manually.
 * @param sl The sorted list object to attach the list to
 * @param list The list to use for sorted lookups
 * @param items The number of items in the list
 * @param sorted Should be 0 if the list needs sorting and 1 otherwise
 * @return 0 on success, -1 if sl is NULL, -2 if sl already has a list
 */
extern int slist_set_list(slist *sl, void **list, uint items, int sorted);

/**
 * free()'s memory associated with the slist table.
 * Note that the slist object itself isn't released. Only the list
 * base pointer. If you've used slist_set_list() to set the list for
 * this sorted list object, you must be careful to ensure that the
 * list can be free'd without causing segmentation violations or
 * memory leaks.
 * @param sl The sorted list object
 */
extern void slist_release(slist *sl);
#endif /* include_slist_h__ */
