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

/*** private helpers ***/
static int safe_write(binlog *bl, void *buf, int len)
{
	int result;
	off_t pos;

	pos = lseek(bl->fd, 0, SEEK_CUR);
	if (pos != bl->file_size)
		lseek(bl->fd, 0, SEEK_END);
	result = write(bl->fd, buf, len);

	if (result == len) {
		bl->file_write_pos = lseek(bl->fd, 0, SEEK_CUR);
		return 0;
	}
	if (result < 0)
		return result;

	/* partial write. this will mess up the sync */
	if (pos != lseek(bl->fd, pos, SEEK_SET)) {
		binlog_invalidate(bl);
		return BINLOG_EINVALID;
	}

	return BINLOG_EINCOMPLETE;
}

/*** public api ***/
int binlog_is_valid(binlog *bl)
{
	return bl->is_valid;
}

void binlog_invalidate(binlog *bl)
{
	binlog_close(bl);
	bl->is_valid = 0;
	unlink(bl->path);
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

	if (!bl)
		return;

	max_mem_size = bl->max_mem_size;
	max_file_size = bl->max_file_size;
	path = bl->path;

	if (!(flags & BINLOG_UNLINK)) {
		binlog_flush(bl);
	}

	binlog_close(bl);

	if (!(flags & BINLOG_UNLINK) || bl->file_read_pos == bl->file_write_pos) {
		unlink(bl->path);
	}

	if (bl->cache) {
		unsigned int i;

		for (i = 0; i < bl->write_index; i++) {
			struct binlog_entry *entry = bl->cache[i];

			if (!entry)
				continue;

			if (entry->data)
				free(entry->data);
			free(entry);
		}
		free(bl->cache);
	}

	memset(bl, 0, sizeof(*bl));
	bl->max_mem_size = max_mem_size;
	bl->max_file_size = max_file_size;
	bl->path = path;
	bl->is_valid = 1;
	bl->fd = -1;
}

void binlog_destroy(binlog *bl, int flags)
{
	if (!bl)
		return;

	binlog_wipe(bl, flags);

	if (bl->path) {
		free(bl->path);
		bl->path = NULL;
	}
	if (bl->file_metadata_path) {
		free(bl->file_metadata_path);
		bl->file_metadata_path = NULL;
	}
	if (bl->file_save_path) {
		free(bl->file_save_path);
		bl->file_save_path = NULL;
	}

	free(bl);
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
	free(bl->cache[bl->read_index]);
	bl->cache[bl->read_index] = NULL;
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

	if (bl->fd != -1)
		return bl->fd;

	if (!bl->path)
		return BINLOG_ENOPATH;

	if (!binlog_is_valid(bl)) {
		bl->file_read_pos = bl->file_write_pos = bl->file_size = 0;
		flags = O_RDWR | O_CREAT | O_TRUNC;
	}

	bl->fd = open(bl->path, flags, 0600);
	if (bl->fd < 0)
		return -1;

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

	/* bail out early if there's no room */
	if (bl->file_size + (off_t)len > bl->max_file_size)
		return BINLOG_ENOSPC;

	ret = binlog_open(bl);
	if (ret < 0)
		return ret;

	ret = safe_write(bl, &len, sizeof(len));
	if (ret)
		return ret;
	ret = safe_write(bl, buf, len);
	bl->file_size += len + sizeof(len);
	bl->file_entries++;

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
	int ret = 0;

	if (!bl)
		return BINLOG_EADDRESS;

	if (bl->fd != -1) {
		ret = close(bl->fd);
		bl->fd = -1;
	}

	return ret;
}

int binlog_flush(binlog *bl)
{
	if (!bl)
		return BINLOG_EADDRESS;

	if (bl->cache) {
		while (bl->read_index < bl->write_index) {
			binlog_entry *entry = bl->cache[bl->read_index++];
			binlog_file_add(bl, entry->data, entry->size);
			free(entry->data);
			free(entry);
		}
		free(bl->cache);
		bl->cache = NULL;
	}
	bl->mem_size = bl->write_index = bl->read_index = bl->alloc = 0;

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

	/* Check if there is a saved binlog to read */
	if( access( node_binlog->file_metadata_path, F_OK ) != 0 ) {
		return NULL;
	}

	if( access( node_binlog->file_save_path, F_OK ) != 0 ) {
		/* For some reason we had a metadata file, but no save file. */
		/* Delete the metadata file */
		unlink(node_binlog->file_metadata_path);
		return NULL;
	}

	file = fopen(node_binlog->file_metadata_path, "rb");
	/* Make sure we could open the file correctly */
	if (file == NULL) {
		return NULL;
	}

	/* free'd either here (binlog_destroy) or by the caller (node_binlog_read_saved) */
	bl = malloc(sizeof(struct binlog));
	if (bl == NULL) {
		fclose(file);
		return NULL;
	}
	memset(bl, 0, sizeof(struct binlog));

	elements_read = fread(bl, sizeof(struct binlog), 1, file);
	fclose(file);

	/* Make sure we sucessfully read one binlog struct */
	if (elements_read != 1) {
		unlink(node_binlog->file_save_path);
		unlink(node_binlog->file_metadata_path);
		free(bl);
		bl = NULL;
		return NULL;																														
	}

	bl->file_metadata_path = strdup(node_binlog->file_metadata_path);
	bl->path = strdup(node_binlog->file_save_path);
	/* Need to reset the file descriptor as it won't be valid anymore */
	bl->fd = -1;
	ret = binlog_open(bl);
	if (ret < 0) {
		binlog_destroy(bl, BINLOG_UNLINK);
		return NULL;
	}
	/* get rid of the metadata file now */
	unlink(node_binlog->file_metadata_path);
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
