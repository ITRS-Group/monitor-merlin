#ifndef INCLUDE_binlog_h
#define INCLUDE_binlog_h
#include <unistd.h>
/**
 * @file binlog.h
 * @brief binary logging functions
 * @defgroup merlin-util Merlin utility functions
 * @ingroup Merlin utility functions
 * @{
 */

typedef unsigned int uint;

/** A binary log. */
typedef struct binlog binlog;

#define BINLOG_APPEND 1
#define BINLOG_UNLINK 2
/**
 * Create a binary logging object
 * @param path The path to store on-disk logs.
 * @param msize The maximum amount of memory used for storing
 *              events in the mem-cache of this backlog.
 * @param fsize The max size files are allowed to grow to.
 * @param flags Decide what to do with an already existing file at path
 * @return A binlog object, or NULL on memory allocation errors.
 */
extern binlog *binlog_create(const char *path, uint msize, uint fsize, int flags);

/**
 * Check if a binary log has unread entries
 * @param bl The binary log to examine
 * @returns 1 if entries are found. 0 otherwise.
 */
extern int binlog_has_entries(binlog *bl);

/**
 * Destroys a binary log, freeing all memory associated with it and
 * optionally unlinking the on-disk log (if any).
 *
 * @param bl The binary log object.
 * @param flags Decides what to do with existing log entries
 * @return 0 on success. < 0 on failure.
 */
extern int binlog_destroy(binlog *bl, int flags);

/**
 * Read the first (sequential) event from the binary log.
 * @param bl The binary log object.
 * @param buf A pointer to the pointer where data will be stored.
 * @param len A pointer to where the size of the logged event will be stored.
 * @return 0 on success. < 0 on failure.
 */
extern int binlog_read(binlog *bl, void **buf, uint *len);

/**
 * Add an event to the binary log.
 * If maximum memory size for the in-memory cache has been, or
 * would have been exceeded by adding the new event, all in-memory
 * events are flushed to disk and the mem-cache is reset.
 * @param bl The binary log object.
 * @param buf A pointer to the data involved in the event.
 * @param len The size of the data to store.
 * @return 0 on success. < 0 on failure.
 */
extern int binlog_add(binlog *bl, void *buf, uint len);

/**
 * Close a file associated to a binary log. In normal circum-
 * stances, files are kept open until binary log is flushed
 * in order to increase performance. This is a means for a
 * program that makes heavy use of file-descriptors to free
 * some up for its normal operations should it ever run out.
 * @param bl The binary log object.
 * @return The return value of close(2).
 */
extern int binlog_close(binlog *bl);

/**
 * Flush in-memory events to disk, releasing all the memory
 * allocated to the events.
 * @param The binary log object.
 * @return 0 on success. < 0 on failure.
 */
extern int binlog_flush(binlog *bl);


/** warning condition for backlog base path having insecure permissions */
#define BINLOG_UNSAFE  1

/** failed to stat() backlog base path or non-empty backlog path */
#define BINLOG_ESTAT   (-1)

/** backlog base path is not a directory */
#define BINLOG_ENOTDIR (-2)

/** backlog_open() requested with no path set */
#define BINLOG_ENOPATH (-3)

/** incomplete write to backlog */
#define BINLOG_EINCOMPLETE (-4)

/** attempt to read from empty binlog */
#define BINLOG_EMPTY (-5)

/**
 * backlog was invalidated due to incomplete write followed by
 * failed lseek(2) to original position
 */
#define BINLOG_EINVALID (-5)

/** An event was dropped for some reason (out of space, most likely) */
#define BINLOG_DROPPED (-6)

/** @} */
#endif
