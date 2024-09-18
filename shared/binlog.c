/**
 * binary logging functions
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include "binlog.h"
#include "logging.h"

struct binlog_entry {
	unsigned int size;
	void *data;
};
typedef struct binlog_entry binlog_entry;
#define entry_size(entry) (entry->size + sizeof(struct binlog_entry))

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

/*** private helpers ***/
static int safe_write(binlog *bl, void *buf, int len)
{
	int result;
	off_t pos;
	
	ldebug("binlog_file_add: start, bl=%p", bl);
	pos = lseek(bl->fd, 0, SEEK_CUR);
	ldebug("binlog_file_add:  lseek, pos=%d", (int) pos);
	if (pos != bl->file_size) {
		ldebug("binlog_file_add:  pos (%d) != file_size (%d)", (int) pos, (int) bl->file_size);
		lseek(bl->fd, 0, SEEK_END);
	}
	ldebug("binlog_file_add:  call write");
	result = write(bl->fd, buf, len);
	ldebug("binlog_file_add:    result=%d", result);
	if (result == len) {
		ldebug("binlog_file_add:  result(%d) == len(%d)", result, len);
		bl->file_write_pos = lseek(bl->fd, 0, SEEK_CUR);
		ldebug("binlog_file_add:  lseek, pos=%d", bl->file_write_pos);
		return 0;
	}
	if (result < 0) {
		ldebug("binlog_file_add:  result is < 0");
		return result;
	}

	/* partial write. this will mess up the sync */
	if (pos != lseek(bl->fd, pos, SEEK_SET)) {
		ldebug("binlog_file_add:  lseek, pos (%d) != lseek pos (%d)", (int) pos, (int) lseek(bl->fd, pos, SEEK_SET));
		binlog_invalidate(bl);
		ldebug("binlog_file_add:  call safe_write");
		return BINLOG_EINVALID;
	}
	ldebug("binlog_file_add: end, incomplete (%d).", BINLOG_EINCOMPLETE);
	return BINLOG_EINCOMPLETE;
}

/*** public api ***/
int binlog_is_valid(binlog *bl)
{
	return bl->is_valid;
}

void binlog_invalidate(binlog *bl)
{
	ldebug("binlog_file_add: start, bl=%p", bl);
	binlog_close(bl);
	bl->is_valid = 0;
	ldebug("binlog_file_add:   unlink path (%s)", bl->path);
	unlink(bl->path);
	ldebug("binlog_file_add: end.");
}

const char *binlog_path(binlog *bl)
{
	return bl->path;
}

int binlog_full_warning(binlog *bl)
{
	if (bl->should_warn_if_full) {
		bl->should_warn_if_full = 0;
		return 1;
	}
	return 0;
}

binlog *binlog_create(const char *path, unsigned long long int msize, unsigned long long int fsize, int flags)
{
	binlog *bl;

	/* can't have a max filesize without a path */
	if (fsize && !path)
		return NULL;

	bl = calloc(1, sizeof(binlog));
	if (!bl)
		return NULL;

	if (fsize && path) {
		bl->path = strdup(path);
		if (!bl->path) {
			free(bl);
			bl = NULL;
			return NULL;
		}
	}

	bl->fd = -1;
	bl->max_mem_size = msize;
	bl->max_file_size = fsize;
	bl->is_valid = 1;
	bl->should_warn_if_full = 1;

	if (asprintf(&bl->file_metadata_path, "%s.meta",path) < 15)
	{
		return NULL;
	}

	if (asprintf(&bl->file_save_path, "%s.save",path) < 15)
	{
		return NULL;
	}

	if (bl->path && (flags & BINLOG_UNLINK))
		unlink(bl->path);

	return bl;
}

void binlog_wipe(binlog *bl, int flags)
{
	unsigned long long int max_mem_size, max_file_size;
	char *path;

	ldebug("binlog_wipe: bl=%p, flags=%d", bl, flags);
	if (!bl) {
		ldebug("binlog_wipe: bl is NULL");
		return;
	}

	ldebug("binlog_wipe:   get info from bl max_mem_size");
	max_mem_size = bl->max_mem_size;
	ldebug("binlog_wipe:   get info from bl max_file_size");
	max_file_size = bl->max_file_size;
	ldebug("binlog_wipe:   get info from bl path");
	path = bl->path;

	//ldebug("binlog_wipe:   path=%s msize=%d fsize=%d", path, max_mem_size, max_file_size);
	if (!(flags & BINLOG_UNLINK)) {
		ldebug("binlog_wipe:   get info from bl");
		binlog_flush(bl);
	}

	binlog_close(bl);

	ldebug("binlog_wipe:   check read vs write pos");
	if (!(flags & BINLOG_UNLINK) || bl->file_read_pos == bl->file_write_pos) {
		ldebug("binlog_wipe:   unlink path (%s)", bl->path);
		unlink(bl->path);
	}

	ldebug("binlog_wipe:   check cache (%p)", bl->cache);
	if (bl->cache) {
		unsigned int i;

		ldebug("binlog_wipe:   check read vs write pos");
		for (i = 0; i < bl->write_index; i++) {
			ldebug("binlog_wipe:   check entry (%d)", i);
			struct binlog_entry *entry = bl->cache[i];

			if (!entry ) {
				ldebug("binlog_wipe:   entry is NULL, skippping...");
				continue;
			}

			ldebug("binlog_wipe:   check entry data (%p)", entry->data);
			if (entry->data) {
				ldebug("binlog_wipe:   free entry data (%p)", entry->data);
				free(entry->data);
				entry->data = NULL;
			}

			ldebug("binlog_wipe:   check entry (%p)", entry);
			if (entry) {
				ldebug("binlog_wipe:   free entry (%p)", entry);
				free(entry);
				entry = NULL;
			}
		}

		ldebug("binlog_wipe:   check cache (%p)", bl->cache);
		if (bl->cache) {
			ldebug("binlog_wipe:   free cache (%p)", bl->cache);
			free(bl->cache);
			bl->cache = NULL;
		}
	}

	ldebug("binlog_wipe:   memset bl ptr");
	memset(bl, 0, sizeof(*bl));
	ldebug("binlog_wipe:   reassign mem size (%d)", max_mem_size);
	bl->max_mem_size = max_mem_size;
	ldebug("binlog_wipe:   reassign file size (%d)", max_file_size);
	bl->max_file_size = max_file_size;
	ldebug("binlog_wipe:   reassign path (%s)", path);
	bl->path = path;
	ldebug("binlog_wipe:   reset is_valid");
	bl->is_valid = 1;
	ldebug("binlog_wipe:   reset fd");
	bl->fd = -1;
	ldebug("binlog_wipe: end.");
}

void binlog_destroy(binlog *bl, int flags)
{
	ldebug("binlog_destroy: start, bl=%p, flags=%d", bl, flags);
	if (!bl) {
		ldebug("binlog_destroy: bl is NULL");
		return;
	}

	binlog_wipe(bl, flags);

	ldebug("binlog_destroy:   path=%s", bl->path);
	if (bl->path) {
		ldebug("binlog_destroy:   free path");
		free(bl->path);
		bl->path = NULL;
	}
	ldebug("binlog_destroy:   meta=%s", bl->file_metadata_path);
	if (bl->file_metadata_path) {
		ldebug("binlog_destroy:   free meta");
		free(bl->file_metadata_path);
		bl->file_metadata_path = NULL;
	}
	ldebug("binlog_destroy:   save=%s", bl->file_save_path);
	if (bl->file_save_path) {
		ldebug("binlog_destroy:   free save");
		free(bl->file_save_path);
		bl->file_metadata_path = NULL;
	}

	if (bl) {
		ldebug("binlog_destroy:   free bl");
		free(bl);
		bl = NULL;
	}
	ldebug("binlog_destroy: end.");
}

static int binlog_file_read(binlog *bl, void **buf, unsigned int *len)
{
	int result;

	/*
	 * if we're done reading the file fully, close and
	 * unlink it so we go back to using memory-based
	 * binlog when we're added to next
	 */
	if (bl->file_read_pos >= bl->file_size) {
		binlog_close(bl);
		bl->file_read_pos = bl->file_write_pos = bl->file_size = 0;
		bl->file_entries = 0;
		unlink(bl->path);
		return BINLOG_EMPTY;
	}

	lseek(bl->fd, bl->file_read_pos, SEEK_SET);
	result = read(bl->fd, len, sizeof(*len));
	if(result < 0)
		return -1;

	*buf = malloc(*len);
	result = read(bl->fd, *buf, *len);
	if(result < 0)
		return -1;

	bl->file_read_pos = lseek(bl->fd, 0, SEEK_CUR);
	bl->file_entries--;

	return 0;
}

static int binlog_mem_read(binlog *bl, void **buf, unsigned int *len)
{
	if (!bl->cache || bl->read_index >= bl->write_index) {
		bl->read_index = bl->write_index = 0;
		return BINLOG_EMPTY;
	}

	if (!bl->cache[bl->read_index]) {
		/* this might cause leaks! */
		bl->read_index = bl->write_index = 0;
		return BINLOG_EINVALID;
	}

	*buf = bl->cache[bl->read_index]->data;
	*len = bl->cache[bl->read_index]->size;
	bl->mem_avail -= *len;

	/* free the entry and mark it as empty */
	if (bl->cache[bl->read_index]) {
		free(bl->cache[bl->read_index]);
		bl->cache[bl->read_index] = NULL;
	}
	bl->read_index++;

	/*
	 * reset the read and write index in case we've read
	 * all entries. This lets us re-use the entry slot
	 * later and not just steadily increase the array
	 * size
	 */
	if (bl->read_index >= bl->write_index) {
		bl->read_index = bl->write_index = 0;
		bl->mem_avail = 0;
	}

	return 0;
}

int binlog_read(binlog *bl, void **buf, unsigned int *len)
{
	if (!bl || !buf || !len)
		return BINLOG_EADDRESS;

	/* don't let users read from an invalidated binlog */
	if (!binlog_is_valid(bl)) {
		return BINLOG_EINVALID;
	}

	/*
	 * reading from memory must come first in order to
	 * maintain sequential ordering. Otherwise we'd
	 * have to flush memory-based entries before
	 * starting to write to file
	 */
	if (!binlog_mem_read(bl, buf, len))
		return 0;

	return binlog_file_read(bl, buf, len);
}

/*
 * This is easy. We just reset file_read_pos to point to the start
 * of the old entry and increment the file_entries counter.
 */
static int binlog_file_unread(binlog *bl, unsigned int len)
{
	bl->file_read_pos -= len;
	bl->file_entries++;
	return 0;
}

/*
 * This is also fairly straightforward. We basically just add the
 * pointer to the previous entry in the memory list
 */
static int binlog_mem_unread(binlog *bl, void *buf, unsigned int len)
{
	binlog_entry *entry;

	/* we can't restore items to an invalid binlog */
	if (!bl || !bl->cache || !binlog_is_valid(bl))
		return BINLOG_EDROPPED;

	/*
	 * In case the entire filebased backlog gets unread(),
	 * we can stash one and only one entry on memory stack,
	 * so return BINLOG_EDROPPED in case bl->read_index is
	 * already 0 and we've pushed bl->write_index to 1.
	 * This is because we have no way of finding out which
	 * slot the entry should have had, so further undo's
	 * will not be in sequential ordering.
	 */
	if (bl->read_index == 0 && bl->write_index == 1)
		return BINLOG_EDROPPED;

	entry = malloc(sizeof(*entry));
	if (!entry)
		return BINLOG_EDROPPED;

	bl->mem_avail += len;
	entry->size = len;
	entry->data = buf;
	if (!bl->read_index) {
		/*
		 * first entry to be pushed back to memory after
		 * filebased binlog was emptied. Sequential messages
		 * can be added later, but we can't handle further
		 * undo's
		 */
		bl->cache[bl->read_index] = entry;
		bl->write_index = 1;
	}
	else {
		bl->cache[--bl->read_index] = entry;
	}
	return 0;
}

int binlog_unread(binlog *bl, void *buf, unsigned int len)
{
	if (!bl || !buf || !len) {
		return BINLOG_EADDRESS;
	}

	/*
	 * if the binlog is empty, adding the entry normally has the
	 * same effect as fiddling around with pointer manipulation
	 */
	if (!binlog_num_entries(bl))
		return binlog_add(bl, buf, len);

	/*
	 * if we've started reading from the file, the entry belongs
	 * there. If we haven't, it belongs on the memory stack
	 */
	if (bl->file_read_pos >= (off_t)len)
		binlog_file_unread(bl, len);

	return binlog_mem_unread(bl, buf, len);
}

unsigned int binlog_num_entries(binlog *bl)
{
	unsigned int entries = 0;

	if (!bl)
		return 0;

	if (bl->file_size && bl->file_read_pos < bl->file_size)
		entries = bl->file_entries;
	if (bl->cache && bl->read_index < bl->write_index)
		entries += bl->write_index - bl->read_index;

	return entries;
}

static int binlog_open(binlog *bl)
{
	int flags = O_RDWR | O_APPEND | O_CREAT;

	ldebug("binlog_open: start, bl=%p", bl);
	if (bl->fd != -1) {
		ldebug("binlog_get_saved: FD already open");
		return bl->fd;
	}

	if (!bl->path) {
		ldebug("binlog_get_saved: Path not set");
		return BINLOG_ENOPATH;
	}

	if (!binlog_is_valid(bl)) {
		ldebug("binlog_get_saved: Invalid");
		bl->file_read_pos = bl->file_write_pos = bl->file_size = 0;
		flags = O_RDWR | O_CREAT | O_TRUNC;
	}

	ldebug("binlog_get_saved:   Read file content to binlog struct");
	bl->fd = open(bl->path, flags, 0600);
	if (bl->fd < 0) {
		ldebug("binlog_get_saved: Unable to open file");
		return -1;
	}

	ldebug("binlog_get_saved: end.");
	return 0;
}

static int binlog_grow(binlog *bl)
{
	bl->alloc = ((bl->alloc + 16) * 3) / 2;
	bl->cache = realloc(bl->cache, sizeof(binlog_entry *) * bl->alloc);
	if (!bl->cache)
		return -1;

	return 0;
}

static int binlog_mem_add(binlog *bl, void *buf, unsigned int len)
{
	binlog_entry *entry;

	if (bl->write_index >= bl->alloc && binlog_grow(bl) < 0)
		return BINLOG_EDROPPED;

	entry = malloc(sizeof(*entry));
	if (!entry)
		return BINLOG_EDROPPED;

	entry->data = malloc(len);
	if (!entry->data) {
		free(entry);
		entry = NULL;
		return BINLOG_EDROPPED;
	}

	entry->size = len;
	memcpy(entry->data, buf, len);
	bl->cache[bl->write_index++] = entry;
	bl->mem_size += entry_size(entry);
	bl->mem_avail += len;

	return 0;
}

static int binlog_file_add(binlog *bl, void *buf, unsigned int len)
{
	int ret;

	ldebug("binlog_file_add: start, bl=%p", bl);
	/* bail out early if there's no room */
	if (bl->file_size + (off_t)len > bl->max_file_size) {
		ldebug("binlog_file_add: file size (%d) exceeds max file size (%d)", bl->file_size + (off_t)len, bl->max_file_size);
		return BINLOG_ENOSPC;
	}

	ldebug("binlog_file_add:   call binlog_open");
	ret = binlog_open(bl);
	ldebug("binlog_file_add:     ret=%d", ret);
	if (ret < 0) {
		ldebug("binlog_file_add: failed to open");
		return ret;
	}

	ldebug("binlog_file_add:   call safe_write");
	ret = safe_write(bl, &len, sizeof(len));
	ldebug("binlog_file_add:     ret=%d", ret);
	if (ret) {
		ldebug("binlog_file_add: return");
		return ret;
	}

	ldebug("binlog_file_add:  call safe_write");
	ret = safe_write(bl, buf, len);
	ldebug("binlog_file_add:    ret=%d", ret);
	bl->file_size += len + sizeof(len);
	ldebug("binlog_file_add:  file size (%d)", bl->file_size);
	bl->file_entries++;
	ldebug("binlog_file_add: end, ret=%d file_entries=%d", ret, bl->file_entries);
	return ret;
}

int binlog_add(binlog *bl, void *buf, unsigned int len)
{
	if (!bl || !buf) {
		return BINLOG_EADDRESS;
	}

	/* don't try to write to an invalid binlog */
	if (!binlog_is_valid(bl)) {
		return BINLOG_EINVALID;
	}

	/*
	 * if we've started adding to the file, we must continue
	 * doing so in order to preserve the parsing order when
	 * reading the events
	 */
	if (bl->fd == -1 && bl->mem_size + len < bl->max_mem_size) {
		return binlog_mem_add(bl, buf, len);
	}

	return binlog_file_add(bl, buf, len);
}

int binlog_close(binlog *bl)
{
	ldebug("binlog_close: start, bl=%p", bl);
	int ret = 0;
	
	if (!bl) {
		ldebug("binlog_close: bl is NULL");
		return BINLOG_EADDRESS;
	}

	if (bl->fd != -1) {
		ldebug("binlog_close:   close fd (%d)", bl->fd);
		ret = close(bl->fd);
		bl->fd = -1;
	}

	ldebug("binlog_close: end, ret=%d", ret);
	return ret;
}

int binlog_flush(binlog *bl)
{
	ldebug("binlog_flush: start, bl=%p", bl);
	if (!bl) {
		ldebug("binlog_flush: bl is NULL");
		return BINLOG_EADDRESS;
	}

	ldebug("binlog_flush:   check cache (%p)", bl->cache);
	if (bl->cache) {
		ldebug("binlog_flush:   read index (%d) < write index (%d)", bl->read_index, bl->write_index);
		while (bl->read_index < bl->write_index) {
			ldebug("binlog_flush:   read index (%d)", bl->read_index);
			binlog_entry *entry = bl->cache[bl->read_index++];
			ldebug("binlog_flush:   call binlog_file_add");
			binlog_file_add(bl, entry->data, entry->size);
			ldebug("binlog_flush:   check entry data (%p)", entry->data);
			if (entry->data) {
				ldebug("binlog_flush:   free entry data (%p)", entry->data);
				free(entry->data);
				entry->data = NULL;
			}
			ldebug("binlog_flush:   check entry (%p)", entry);
			if (entry) {
				ldebug("binlog_flush:   free entry (%p)", entry);
				free(entry);
				entry = NULL;
			}
		}
		ldebug("binlog_flush:   check cache (%p)", bl->cache);
		if (bl->cache) {
			ldebug("binlog_flush:   free cache (%p)", bl->cache);
			free(bl->cache);
			bl->cache = NULL;
		}
	}
	ldebug("binlog_flush:   reset members to 0");
	bl->mem_size = bl->write_index = bl->read_index = bl->alloc = 0;

	ldebug("binlog_flush: end.");
	return 0;
}

unsigned int binlog_msize(binlog *bl)
{
	return bl ? bl->mem_size : 0;
}

unsigned int binlog_fsize(binlog *bl)
{
	return bl ? bl->file_size : 0;
}

unsigned int binlog_size(binlog *bl)
{
	return binlog_fsize(bl) + binlog_msize(bl);
}

unsigned int binlog_available(binlog *bl)
{
	if (!bl)
		return 0;

	return (bl->file_size - bl->file_read_pos) + bl->mem_avail;
}

static int binlog_write_file_metadata(binlog *bl) {
	FILE *file;

	binlog_flush(bl);
	file = fopen(bl->file_metadata_path, "wb");
	if (file != NULL) {
		fwrite(bl, sizeof(binlog), 1, file);
		fclose(file);
	} else {
		return -1;
	}

	return 0;
}

binlog * binlog_get_saved(binlog * node_binlog) {
	int ret, elements_read;
	struct binlog *bl;
	FILE * file;

	ldebug("binlog_get_saved: start, node_binlog=%p", node_binlog);
	/* Check if there is a saved binlog to read */
	if( access( node_binlog->file_metadata_path, F_OK ) != 0 ) {
		ldebug("binlog_get_saved: No saved meta file.");
		return NULL;
	}

	if( access( node_binlog->file_save_path, F_OK ) != 0 ) {
		/* For some reason we had a metadata file, but no save file. */
		/* Delete the metadata file */
		ldebug("binlog_get_saved: No save file. Delete metadata file (%s).", node_binlog->file_metadata_path);
		unlink(node_binlog->file_metadata_path);
		return NULL;
	}

	ldebug("binlog_get_saved:   Open meta file (%s).", node_binlog->file_metadata_path);
	file = fopen(node_binlog->file_metadata_path, "rb");
	/* Make sure we could open the file correctly */
	if (file == NULL) {
		ldebug("binlog_get_saved: Unable to open meta file.");
		return NULL;
	}

	/* free'd either here (binlog_destroy) or by the caller (node_binlog_read_saved) */
	ldebug("binlog_get_saved:   Alloc binlog struct");
	bl = malloc(sizeof(struct binlog));
	if (bl == NULL) {
		ldebug("binlog_get_saved: Failed to open meta file.");
		fclose(file);
		return NULL;
	}

	ldebug("binlog_get_saved:   Read file content (%p) to binlog struct", file);
	elements_read = fread(bl, sizeof(struct binlog), 1, file);
	ldebug("binlog_get_saved:   File elements %d. Close file.", elements_read);
	fclose(file);

	/* Make sure we sucessfully read one binlog struct */
	ldebug("binlog_get_saved:   Read file content to binlog struct");
	if (elements_read != 1) {
		binlog_destroy(bl,BINLOG_UNLINK);
		return NULL;
	}

	ldebug("binlog_get_saved:   Dup meta file path");
	bl->file_metadata_path = strdup(node_binlog->file_metadata_path);
	ldebug("binlog_get_saved:   Dup save file path");
	bl->path = strdup(node_binlog->file_save_path);
	/* Need to reset the file descriptor as it won't be valid anymore */
	ldebug("binlog_get_saved:   Set FD to -1");
	bl->fd = -1;
	ret = binlog_open(bl);
	ldebug("binlog_get_saved:   binlog_open ret=%d", ret);
	if (ret < 0) {
		binlog_destroy(bl, BINLOG_UNLINK);
		return NULL;
	}
	/* get rid of the metadata file now */
	ldebug("binlog_get_saved:   Delete meta file (%s)", node_binlog->file_metadata_path);
	unlink(node_binlog->file_metadata_path);
	ldebug("binlog_get_saved: end, bl=%p", bl);
	return bl;
}

int binlog_save(binlog *bl) {
	if (bl && binlog_num_entries(bl) > 0) {
		if (binlog_write_file_metadata(bl) != 0) {
			return -1;
		}
		if (rename(bl->path, bl->file_save_path) != 0) {
			return -1;
		}
	}
	return 0;
}
