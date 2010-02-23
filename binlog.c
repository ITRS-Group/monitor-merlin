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
#include <stdio.h> /* for debugging only */
#include "binlog.h"

struct binlog_entry {
	size_t size;
	void *data;
};
typedef struct binlog_entry binlog_entry;
#define entry_size(entry) (entry->size + sizeof(entry->size))

struct binlog {
	struct binlog_entry **cache;
	uint write_index, read_index;
	uint max_slots, alloc;
	uint max_mem_usage;
	uint mem_size, max_mem_size;
	uint file_size, max_file_size;
	off_t file_read_pos, file_write_pos;
	int is_valid;
	char *path;
	int fd;
};

#if 0
static char *base_path;

static int binlog_set_base_path(const char *path)
{
	uint len;
	struct stat st;
	int result = 0;

	len = strlen(path);

	if (stat(path, &st) < 0)
		return BINLOG_ESTAT;

	if (!S_ISDIR(st.st_mode))
		return BINLOG_ENOTDIR;

	if (st.st_mode & S_IROTH || st.st_mode & S_IWOTH)
		result = BINLOG_UNSAFE;

	if (base_path)
		free(base_path);

	base_path = malloc(len + 2);
	memcpy(base_path, path, len);
	if (path[len] != '/') {
		base_path[len++] = '/';
	}
	base_path[len++] = '\0';

	return result;
}
#endif

/*** private helpers ***/

static void binlog_invalidate(binlog *bl)
{
	close(bl->fd);
	bl->fd = -1;
	bl->is_valid = 0;
	unlink(bl->path);
}

static int safe_write(binlog *bl, void *buf, uint len)
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

static inline int binlog_is_valid(binlog *bl)
{
	return bl->is_valid;
}

binlog *binlog_create(const char *path, uint msize, uint fsize, int flags)
{
	binlog *bl;

	bl = calloc(1, sizeof(binlog));
	if (!bl)
		return NULL;
	bl->fd = -1;
	bl->path = strdup(path);
	bl->max_mem_size = msize;
	bl->max_file_size = fsize;

	if (flags & BINLOG_UNLINK)
		unlink(bl->path);

	return bl;
}

int binlog_destroy(binlog *bl, int keep_file)
{
	if (keep_file) {
		binlog_flush(bl);
	}

	if (bl->fd != -1) {
		close(bl->fd);
		bl->fd = -1;
	}

	if (!keep_file || bl->file_read_pos == bl->file_write_pos) {
		unlink(bl->path);
	}

	if (bl->cache) {
		uint i;

		for (i = 0; i < bl->write_index; i++) {
			if (bl->cache[i])
				free(bl->cache[i]);
		}
		free(bl->cache);
	}
	memset(bl, 0, sizeof(*bl));
	free(bl);

	return 0;
}

static int binlog_file_read(binlog *bl, void **buf, uint *len)
{
	int result;

	if (bl->file_read_pos >= bl->file_size)
		return BINLOG_EMPTY;

	lseek(bl->fd, bl->file_read_pos, SEEK_SET);
	result = read(bl->fd, len, sizeof(*len));
	*buf = malloc(*len);
	result = read(bl->fd, *buf, *len);
	bl->file_read_pos = lseek(bl->fd, 0, SEEK_CUR);

	return 0;
}

int binlog_read(binlog *bl, void **buf, uint *len)
{
	/*
	 * reading from file must come first in order to
	 * maintain sequential ordering
	 */
	if (bl->file_size && bl->file_read_pos < bl->file_size) {
		return binlog_file_read(bl, buf, len);
	}

	if (bl->cache && bl->read_index < bl->write_index) {
		*buf = bl->cache[bl->read_index]->data;
		*len = bl->cache[bl->read_index]->size;
		bl->read_index++;
		return 0;
	}

	return BINLOG_EMPTY;
}

int binlog_has_entries(binlog *bl)
{
	if (!bl)
		return 0;

	if (bl->file_size && bl->file_read_pos < bl->file_size)
		return 1;
	if (bl->cache && bl->read_index < bl->write_index)
		return 1;

	return 0;
}

static int binlog_open(binlog *bl)
{
	int flags = O_APPEND | O_CREAT;

	if (bl->fd != -1)
		return bl->fd;

	if (!binlog_is_valid(bl))
		flags = O_CREAT | O_TRUNC | O_WRONLY;

	if (!bl->path)
		return BINLOG_ENOPATH;

	bl->fd = open(bl->path, O_RDWR | O_APPEND | O_CREAT, 0600);
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

static int binlog_mem_add(binlog *bl, void *buf, uint len)
{
	binlog_entry *entry;
	entry = malloc(sizeof(*entry));
	entry->size = len;

	entry->data = malloc(len);
	memcpy(entry->data, buf, len);

	if (bl->write_index >= bl->alloc)
		binlog_grow(bl);

	bl->cache[bl->write_index++] = entry;
	bl->mem_size += entry_size(entry);

	return 0;
}

static int binlog_file_add(binlog *bl, void *buf, uint len)
{
	int ret;

	ret = binlog_open(bl);
	if (ret < 0)
		return ret;

	ret = safe_write(bl, &len, sizeof(len));
	if (ret)
		return ret;
	ret = safe_write(bl, buf, len);
	fsync(bl->fd);
	bl->file_size += len + sizeof(len);

	return ret;
}

int binlog_add(binlog *bl, void *buf, uint len)
{
	if (bl->fd == -1 && bl->mem_size < bl->max_mem_size) {
		return binlog_mem_add(bl, buf, len);
	}

	binlog_flush(bl);
	return binlog_file_add(bl, buf, len);
}

int binlog_close(binlog *bl)
{
	int ret = 0;

	if (bl->fd != -1) {
		ret = close(bl->fd);
		bl->fd = -1;
	}

	return ret;
}

int binlog_flush(binlog *bl)
{
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
