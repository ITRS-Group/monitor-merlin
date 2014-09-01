#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "compat.h"
#include "lparse.h"

static char *buf;
#define MAX_BUF ((512 * 1024) - 1)

int lparse_fd(int fd, uint64_t size, int (*parse)(char *, uint))
{
	uint64_t tot_rd = 0;

	if (!buf && !(buf = calloc(1, MAX_BUF + 1)))
		return -1;

	while (tot_rd < size) {
		char *next, *cur;
		int blkparsed, bytes_rd;

		bytes_rd = read(fd, buf, MAX_BUF < size ? MAX_BUF : size);
		if (bytes_rd < 0)
			return -1;

		if (tot_rd + bytes_rd < size) {
			/* rewind the file to the last found newline */
			while (buf[bytes_rd - 1] != '\n')
				bytes_rd--;
			lseek(fd, tot_rd + bytes_rd, SEEK_SET);
		}
		tot_rd += bytes_rd;

		/* set a sentinel for memchr() */
		buf[bytes_rd] = '\n';

		cur = buf;
		for (blkparsed = 0; blkparsed < bytes_rd; cur = next + 1) {
			uint len;
			next = memchr(cur, '\n', bytes_rd + 1 - blkparsed);
			len = next - cur;
			blkparsed += len + 1;

			cur[len] = '\0';
			parse(cur, len);
		}
	}

	return 0;
}

int lparse_rev_fd(int fd, uint64_t size, int (*parse)(char *, uint))
{
	uint64_t tot_rd = 0, parsed = 0, reads = 0;

	if (!buf && !(buf = calloc(1, MAX_BUF + 1)))
		return -1;

	while (tot_rd < size) {
		char *cur, *first, *eol, *last;
		int blkparsed, bytes_rd;
		size_t to_read;

		/*
		 * First we figure out where to start reading
		 * and how much to read. Then we lseek() to that
		 * position and actually read it in (works)
		 */
		if (tot_rd + MAX_BUF < size) {
			to_read = MAX_BUF;
		} else {
			to_read = size - tot_rd;
		}
		lseek(fd, size - tot_rd - to_read, SEEK_SET);
		reads++;
		bytes_rd = read(fd, buf, to_read);
		if (bytes_rd < 0)
			return -1;

		if (tot_rd + bytes_rd < size) {

			/*
			 * set 'first' to just after first newline or,
			 * failing that, to the start of the buffer itself
			 */
			first = memchr(buf, '\n', bytes_rd);
			if (!first)
				first = buf;
			 else
				first++;

			/* remember the position of the first found newline */
			bytes_rd -= first - buf;
		} else {
			first = buf;
		}
		tot_rd += bytes_rd;

		/*
		 * if the buffer we just read ends with a newline, we must
		 * discard it from the first round of parsing, or we'll add
		 * one line for each time we read.
		 */
		if (first[bytes_rd - 1] == '\n')
			--bytes_rd;

		eol = last = first + bytes_rd;
		for (blkparsed = 0; blkparsed < bytes_rd; cur = eol - 1) {
			uint len;
			char *line;

			/*
			 * set 'cur' to first newline befor 'eol', and set
			 * 'line' to first char after it
			 */
			cur = memrchr(first, '\n', bytes_rd - blkparsed);
			if (!cur) {
				line = cur = first;
			} else {
				line = cur + 1;
			}

			len = eol - line;
			blkparsed += len + 1;

			*eol = '\0';
			parse(line, len);
			eol = cur;
		}
		parsed += bytes_rd;
	}

	return 0;
}

int lparse_path_real(int rev, const char *path, uint64_t size, int (*parse)(char *, uint))
{
	int fd, result;

	/* zero size files are never interesting */
	if (!size)
		return 0;

	if ((fd = open(path, O_RDONLY)) < 0)
		return -1;

	if (rev)
		result = lparse_rev_fd(fd, size, parse);
	else
		result = lparse_fd(fd, size, parse);

	close(fd);

	return result;
}
