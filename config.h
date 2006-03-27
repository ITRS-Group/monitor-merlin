#ifndef _CONFIG_H
#define _CONFIG_H

/* have a care with this one. It can't be used when (c) has side-effects */
#undef ISSPACE
#define ISSPACE(c) ((c) == ' ' || (c) == '\t')

/* types */
struct cfg_var {
	unsigned line; /* starting line */
	char *var;
	char *val;
	char *val_end; /* for line continuation */
};

struct compound {
	char *name;               /* filename, or everything leading up to '{' */
	char *buf;                /* file contents, if this is a file */
	unsigned int vars;        /* number of variables */
	unsigned int vlist_len;   /* size of vlist */
	unsigned start, end;      /* starting and ending line */
	unsigned nested;          /* number of compounds nested below this */
	struct cfg_var **vlist;   /* array of variables */
	struct compound *parent;  /* nested from */
	struct compound **nest;   /* compounds nested inside this one */
};

/* prototypes */
struct compound *cfg_parse_file(char *path);
void cfg_destroy_compound(struct compound *comp);
void cfg_warn(struct compound *comp, struct cfg_var *v, const char *fmt, ...)
	__attribute__((__format__(__printf__, 3, 4)));
void cfg_error(struct compound *comp, struct cfg_var *v, const char *fmt, ...)
	__attribute__((__format__(__printf__, 3, 4), __noreturn__));

#endif /* _CONFIG_H */
