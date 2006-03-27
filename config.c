/*
 * read nested compound configuration files
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <ctype.h>
#include <glob.h>

#include "config.h"

static struct compound *parse_file(char *path, struct compound *parent, unsigned line);

/* read a file and return it in a buffer. Size is stored in *len.
 * If there are errors, return NULL and set *len to -errno */
static char *cfg_read_file(char *path, unsigned *len)
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

	/* force NUL-termination and insert a newline if there is none */
	buf[st.st_size] = '\n';
	buf[st.st_size + 1] = '\0';
	*len = st.st_size;

	return buf;
}

/* handles the 'include' syntax. glob() must be available */
static struct compound *include(const char *pattern, struct compound *parent, unsigned line)
{
	struct compound *comp;
	glob_t gl;
	size_t i;
	int result;

	if (!parent)
		return NULL;

	result = glob(pattern, 0, NULL, &gl);

	/* do error checking here */
	for (i = 0; i < gl.gl_pathc; i++)
		comp = parse_file(gl.gl_pathv[i], parent, line);

	return parent;
}

static struct compound *start_compound(char *name, struct compound *cur, unsigned line)
{
	struct compound *comp = calloc(1, sizeof(struct compound));

	if (comp) {
		comp->start = line;
		comp->name = name;
		comp->parent = cur;
	}

	if (cur) {
		cur->nested++;
		cur->nest = realloc(cur->nest, sizeof(struct compound *) * cur->nested);
		cur->nest[cur->nested - 1] = comp;
	}

	return comp;
}

static struct compound *close_compound(struct compound *comp, unsigned line)
{
	if (comp) {
		comp->end = line;
		return comp->parent;
	}

	return NULL;
}

static void add_var(struct compound *comp, struct cfg_var *v)
{
	/* the "include" directive has special meaning */
	if (!strcmp(v->var, "include")) {
		include(v->val, comp, v->line);
		return;
	}

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
		if (last != '\\' && (*s == ';' || *s == '{' || *s == '}')) {
			return s;
		}
		last = *s;
	}

	return NULL;
}

static struct compound *parse_file(char *path, struct compound *parent, unsigned line)
{
	unsigned buflen, i, lnum = 0, lcont = 0;
	char *buf = cfg_read_file(path, &buflen);
	struct cfg_var v;
	struct compound *comp = start_compound(path, parent, 0);
	unsigned compound_depth = 0;
	char end = '\n'; /* count like cat -n */

	if (!buf || !comp) {
		if (comp)
			free(comp);
		if (buf)
			free(buf);

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
			v.var = v.val = v.val_end = NULL;
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

		if (end == '{') {
			lend[1] = '\0';
			v.val_end = v.var = v.val = NULL;
			compound_depth++;
			comp = start_compound(lstart, comp, lnum);
			i = next - buf;
			continue;
		}

		/* start of compound or line continuation */
		if (*lend == '\\' && end == '\n') {
			*lend = ' ';
			while (ISSPACE(*lend))
				lend--;

			lcont |= 2;
		}

		if (lcont & 1) {
			unsigned len = (lend - lstart) + 1;
			*v.val_end++ = ' ';
			memmove(v.val_end, lstart, len);
			v.val_end += len;
			*v.val_end = '\0';
		}
		lcont >>= 1 & 1;

		if (!v.var) {
			char *p = lstart + 1;

			v.line = lnum;
			v.var = lstart;
			v.val_end = lend;
			while (p < lend && !ISSPACE(*p) && *p != '=')
				p++;
			if (ISSPACE(*p) || *p == '=') {
				while(p < lend && (ISSPACE(*p) || *p == '='))
					*p++ = '\0';
				if (*p && p <= lend) {
					v.val = p;
					*v.val_end++;
				}
			}
		}

		if (v.var && *v.var && !lcont) {
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

static void cfg_print_error(struct compound *comp, struct cfg_var *v,
                            const char *fmt, va_list ap)
{
	struct compound *c;

	fprintf(stderr, "*** Configuration error\n");
	if (v)
		fprintf(stderr, "  on line %d, near '%s' = '%s'\n",
				v->line, v->var, v->val);

	if (!comp->buf)
		fprintf(stderr, "  in compound '%s' starting on line %d\n", comp->name, comp->start);

	fprintf(stderr, "  in file ");
	for (c = comp; c; c = c->parent) {
		if (c->buf) {
			fprintf(stderr, "'%s'", c->name);
			if (c->parent)
				fprintf(stderr, ", included from line %d in\n  ", c->start);
			else
				fprintf(stderr, "\n");
		}
	}

	fprintf(stderr, "----\n");
	vfprintf(stderr, fmt, ap);
	if (fmt[strlen(fmt) - 1] != '\n')
		fputc('\n', stderr);
	fprintf(stderr, "----\n");
}

/** public functions **/
void cfg_warn(struct compound *comp, struct cfg_var *v, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	cfg_print_error(comp, v, fmt, ap);
	va_end(ap);
}

void cfg_error(struct compound *comp, struct cfg_var *v, const char *fmt, ...)
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
void cfg_destroy_compound(struct compound *comp)
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
		struct compound *parent = comp->parent;
		memset(comp, 0, sizeof(struct compound));
		comp->parent = parent;
	}
}

struct compound *cfg_parse_file(char *path)
{
	struct compound *comp = parse_file(path, NULL, 0);

	/* this is the public API, so make sure all compounds are closed */
	if (comp && comp->parent) {
		cfg_error(comp, NULL, "Unclosed compound (there may be more)\n");
		return NULL;
	}

	return comp;
}
