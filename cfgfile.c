#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "cfgfile.h"

static struct cfg_comp *parse_file(const char *path, struct cfg_comp *parent, unsigned line);

/* read a file and return it in a buffer. Size is stored in *len.
 * If there are errors, return NULL and set *len to -errno */
static char *cfg_read_file(const char *path, unsigned *len)
{
	int fd, rd = 0, total = 0;
	struct stat st;
	char *buf = NULL;

	if (access(path, R_OK) < 0) {
		*len = -errno;
		fprintf(stderr, "Failed to access '%s': %s\n", path, strerror(errno));
		return NULL;
	}

	/* open, stat, malloc, read. caller handles errors (errno will be set) */
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		*len = -errno;
		fprintf(stderr, "Failed to open '%s': %s\n", path, strerror(errno));
		return NULL;
	}

	if (fstat(fd, &st) < 0) {
		*len = -errno;
		fprintf(stderr, "Failed to stat '%s': %s\n", path, strerror(errno));
		close(fd);
		return NULL;
	}

	/* make room for a forced newline and null-termination */
	buf = malloc(st.st_size + 3);
	if (!buf) {
		*len = -errno;
		fprintf(stderr, "Failed to allocate %lu bytes of memory for '%s'\n",
				st.st_size, path);
		close(fd);
		return NULL;
	}

	do {
		rd = read(fd, buf + rd, st.st_size - rd);
		total += rd;
	} while (total < st.st_size && rd > 0);

	/* preserve errno, so close() doesn't alter it */
	*len = errno;
	close(fd);

	if (rd < 0 || total != st.st_size) {
		fprintf(stderr, "Reading from '%s' failed: %s\n", path, strerror(*len));
		free(buf);
		return NULL;
	}

	/* force newline+nul at EOF */
	buf[st.st_size] = '\n';
	buf[st.st_size + 1] = '\0';
	*len = st.st_size;

	return buf;
}

static struct cfg_comp *start_compound(const char *name, struct cfg_comp *cur, unsigned line)
{
	struct cfg_comp *comp = calloc(1, sizeof(struct cfg_comp));

	if (comp) {
		comp->start = line;
		comp->name = name;
		comp->parent = cur;
	}

	if (cur) {
		cur->nested++;
		cur->nest = realloc(cur->nest, sizeof(struct cfg_comp *) * cur->nested);
		cur->nest[cur->nested - 1] = comp;
	}

	return comp;
}

static struct cfg_comp *close_compound(struct cfg_comp *comp, unsigned line)
{
	if (comp) {
		comp->end = line;
		return comp->parent;
	}

	return NULL;
}

static void add_var(struct cfg_comp *comp, struct cfg_var *v)
{
	if (comp->vars >= comp->vlist_len) {
		comp->vlist_len += 5;
		comp->vlist = realloc(comp->vlist, sizeof(struct cfg_var *) * comp->vlist_len);
	}

	comp->vlist[comp->vars] = malloc(sizeof(struct cfg_var));
	memcpy(comp->vlist[comp->vars++], v, sizeof(struct cfg_var));
}

static inline char *end_of_line(char *s)
{
	char last = 0;

	for (; *s; s++) {
		if (*s == '\n')
			return s;
		if (last != '\\') {
			if (*s == ';') {
				*s = '\0';
			}
			else {
				if (*s == '{' || *s == '}')
					return s;
			}
		}
		last = *s;
	}

	return NULL;
}

static struct cfg_comp *parse_file(const char *path, struct cfg_comp *parent, unsigned line)
{
	unsigned compound_depth = 0, buflen, i, lnum = 0;
	char *buf;
	struct cfg_var v;
	struct cfg_comp *comp;
	char end = '\n'; /* count like cat -n */

	if (!(comp = start_compound(path, parent, 0)))
		return NULL;

	if (!(buf = cfg_read_file(path, &buflen))) {
		free(comp);
		return NULL;
	}

	comp->buf = buf; /* save a pointer to free() later */
	comp->start = comp->end = line;

	memset(&v, 0, sizeof(v));
	for (i = 0; i < buflen; i++) {
		char *next, *lstart, *lend;

		if (end == '\n')
			lnum++;

		/* skipe whitespace */
		while(ISSPACE(buf[i]))
			i++;

		/* skip comments */
		if (buf[i] == '#') {
			while(buf[i] != '\n')
				i++;

			end = '\n';
			continue;
		}

		/* hop empty lines */
		if (buf[i] == '\n') {
			v.key = v.value = NULL;
			end = '\n';
			continue;
		}

		next = lend = end_of_line(&buf[i]);
		end = *lend;
		lstart = &buf[i];

		/* nul-terminate and strip space from end of line */
		*lend-- = '\0';
		while(ISSPACE(*lend))
			lend--;

		/* check for start of compound */
		if (end == '{') {
			v.key = v.value = NULL;
			compound_depth++;
			comp = start_compound(lstart, comp, lnum);
			i = next - buf;
			continue;
		}

		if (!v.key) {
			char *p = lstart + 1;

			v.line = lnum;
			v.key = lstart;

			while (p < lend && !ISSPACE(*p) && *p != '=')
				p++;

			if (ISSPACE(*p) || *p == '=') {
				v.key_len = p - &buf[i];
				while(p < lend && (ISSPACE(*p) || *p == '='))
					*p++ = '\0';

				if (*p && p <= lend)
					v.value = p;
			}
		}

		if (v.key && *v.key) {
			if (v.value)
				v.value_len = 1 + lend - v.value;
			add_var(comp, &v);
			memset(&v, 0, sizeof(v));
		}

		if (end == '}') {
			comp = close_compound(comp, lnum);
			compound_depth--;
		}

		i = next - buf;
	}

	return comp;
}

static void cfg_print_error(struct cfg_comp *comp, struct cfg_var *v,
                            const char *fmt, va_list ap)
{
	struct cfg_comp *c;

	fprintf(stderr, "*** Configuration error\n");
	if (v)
		fprintf(stderr, "  on line %d, near '%s' = '%s'\n",
				v->line, v->key, v->value);

	if (!comp->buf)
		fprintf(stderr, "  in compound '%s' starting on line %d\n", comp->name, comp->start);

	fprintf(stderr, "  in file ");
	for (c = comp; c; c = c->parent) {
		if (c->buf)
			fprintf(stderr, "'%s'", c->name);
	}

	fprintf(stderr, "----\n");
	vfprintf(stderr, fmt, ap);
	if (fmt[strlen(fmt) - 1] != '\n')
		fputc('\n', stderr);
	fprintf(stderr, "----\n");
}

/** public functions **/

/* this is significantly faster than doing strdup(), since
 * it can copy word-wise rather than byte-wise */
char *cfg_copy_value(struct cfg_var *v)
{
	char *ptr;

	if ((ptr = calloc(v->value_len + 1, 1)))
		return memcpy(ptr, v->value, v->value_len);

	return NULL;
}

void cfg_warn(struct cfg_comp *comp, struct cfg_var *v, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	cfg_print_error(comp, v, fmt, ap);
	va_end(ap);
}

void cfg_error(struct cfg_comp *comp, struct cfg_var *v, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	cfg_print_error(comp, v, fmt, ap);
	va_end(ap);

	exit (1);
}

/* releases a compound and all its nested compounds recursively
 * Note that comp->name is never free()'d since it either
 * points to somewhere in comp->buf or is obtained from the caller
 * and may point to the stack of some other function */
void cfg_destroy_compound(struct cfg_comp *comp)
{
	unsigned i;

	if (!comp)
		return;

	/* free() children so this can be entered anywhere in the middle */
	for (i = 0; i < comp->nested; i++) {
		cfg_destroy_compound(comp->nest[i]);
		free(comp->nest[i]);
	}

	for (i = 0; i < comp->vars; i++)
		free(comp->vlist[i]);

	if (comp->vlist)
		free(comp->vlist);

	if (comp->buf)
		free(comp->buf);

	if (comp->nest)
		free(comp->nest);

	if (!comp->parent)
		free(comp);
	else {
		/* If there is a parent we'll likely enter this compound again.
		 * If so, it mustn't try to free anything or read from any list,
		 * so zero the entire compound, but preserve the parent pointer. */
		struct cfg_comp *parent = comp->parent;
		memset(comp, 0, sizeof(struct cfg_comp));
		comp->parent = parent;
	}
}

struct cfg_comp *cfg_parse_file(const char *path)
{
	struct cfg_comp *comp = parse_file(path, NULL, 0);

	/* this is the public API, so make sure all compounds are closed */
	if (comp && comp->parent) {
		cfg_error(comp, NULL, "Unclosed compound (there may be more)\n");
		return NULL;
	}

	return comp;
}
