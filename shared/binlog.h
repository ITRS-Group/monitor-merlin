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

/** A binary log. */
struct binlog {
	struct binlog_entry **cache;
	unsigned int write_index, read_index, file_entries;
	unsigned int alloc;
	unsigned int mem_size;
	unsigned long long int max_mem_size;
	unsigned int mem_avail;
	off_t max_file_size, file_size, file_read_pos, file_write_pos;
	int is_valid;
	int should_warn_if_full;
	char *path;
	char *file_metadata_path;
	char *file_save_path;
	int fd;
};
typedef struct binlog binlog;

#define BINLOG_APPEND 1
#define BINLOG_UNLINK 2

/**
 * Check if binlog is valid.
 * "valid" in this case means "has it escaped being invalidated"?
 * @param bl The binary log to check for validity
 * @return 1 if the binlog is valid, 0 otherwise
 */
extern int binlog_is_valid(binlog *bl);

/**
 * Invalidate the binlog
 * This is useful for applications that requires their binlogs to
 * be sequential.
 * @param bl The binary log to operate on
 */
extern void binlog_invalidate(binlog *bl);

/**
 * Get the on-disk binlog storage path
 * @param bl The binary log whose path we should return
 * @return The on-disk binlog storage path
 */
extern const char *binlog_path(binlog *bl);

/**
 * Check to make sure that only one warning is logged when
 * the binlog is full, so the log isn't spammed.
 * @param bl The binary log to operate on
 * @return 1 if binlog just became full, 0 if it has already warned
 */
extern int binlog_full_warning(binlog *bl);

/**
 * Create a binary logging object. If fsize is 0, path may be NULL.
 * @param path The path to store on-disk logs.
 * @param msize The maximum amount of memory used for storing
 *              events in the mem-cache of this backlog.
 * @param fsize The max size files are allowed to grow to.
 * @param flags Decide what to do with an already existing file at path
 * @return A binlog object on success, NULL on errors.
 */
extern binlog *binlog_create(const char *path, unsigned long long int msize, unsigned long long int fsize, int flags);

/**
 * Get the number of unread entries in the binlog
 * @param bl The binary log to examine
 * @returns Number of entries in binlog
 */
extern unsigned int binlog_num_entries(binlog *bl);
#define binlog_has_entries(bl) binlog_num_entries(bl)
#define binlog_entries(bl) binlog_num_entries(bl)

/**
 * Wipes a binary log, freeing all memory associated with it and
 * restoring the old defaults. Also validates the binlog again,
 * making it re-usable.
 * @param bl The binlog to wipe
 * @param flags takes BINLOG_UNLINK to remove the file from disk
 */
extern void binlog_wipe(binlog *bl, int flags);

/**
 * Destroys a binary log, freeing all memory associated with it and
 * optionally unlinking the on-disk log (if any).
 * @param bl The binary log object.
 * @param flags Takes BINLOG_UNLINK to remove the file from disk
 * @return 0 on success. < 0 on failure.
 */
extern void binlog_destroy(binlog *bl, int flags);

/**
 * Read the first (sequential) event from the binary log.
 * @param bl The binary log object.
 * @param buf A pointer to the pointer where data will be stored.
 * @param len A pointer to where the size of the logged event will be stored.
 * @return 0 on success. < 0 on failure.
 */
extern int binlog_read(binlog *bl, void **buf, unsigned int *len);

/**
 * "unread" one entry from the binlog. This lets one maintain
 * sequential reading from the binlog even when event processing
 * fails. Note that the data isn't duplicated again here, since
 * the most common case is that the recently read data is pushed
 * back immediately after whatever action was supposed to be
 * taken on it has failed.
 * @param bl The binlog to unread() from/to
 * @param buf The data to unread
 * @param len The length of the data to read
 * @return 0 on success. < 0 on failure.
 */
extern int binlog_unread(binlog *bl, void *buf, unsigned int len);

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
extern int binlog_add(binlog *bl, void *buf, unsigned int len);

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
 * @param bl The binary log object.
 * @return 0 on success. < 0 on failure.
 */
extern int binlog_flush(binlog *bl);

/**
 * Get binlog memory consumption size
 * @param bl The binary log object.
 * @return memory consumption
 */
extern unsigned int binlog_msize(binlog *bl);

/**
 * Get on-disk binlog size
 * @param bl The binary log object.
 * @return disk storage consumption
 */
extern unsigned int binlog_fsize(binlog *bl);

/**
 * Get binlog size
 * @param bl The binary log object.
 * @return disk storage consumption and memory consumption
 */
extern unsigned int binlog_size(binlog *bl);

/**
 * Get number of available entries in the binlog
 * @param bl The binary log objects.
 * @return Number of bytes available for reading from this log
 */
extern unsigned int binlog_available(binlog *bl);

/**
 * Creates a new binlog from a binlog that was persistently saved on disk.
 * @param bl the normal binlog for a node
 * @return the persistently saved binlog
 */
extern binlog * binlog_get_saved(binlog * node_binlog); 

/**
 * Saves a binlog persistently on disk.
 * @param bl the binlog to save
 * @return 0 on success -1 otherwise
 */
extern int binlog_save(binlog *bl);

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
#define BINLOG_EINVALID (-6)

/** Binlog is full but attempted to write to it anyway */
#define BINLOG_ENOSPC (-7)

/** An event was dropped for some reason (not out-of-space) */
#define BINLOG_EDROPPED (-8)

/** A NULL pointer was passed when a pointer was expected */
#define BINLOG_EADDRESS (-9)

/** @} */
#endif
