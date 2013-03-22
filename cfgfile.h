#ifndef INCLUDE_cfgfile_h__
#define INCLUDE_cfgfile_h__

#include <stdlib.h>

/* types */
struct cfg_var {
	unsigned line; /* starting line */
	char *key;
	char *value;
	size_t key_len;
	size_t value_len;
};

struct cfg_comp {
	char *name;               /* compound name */
	char *buf;                /* file contents, if this is a file */
	unsigned int vars;        /* number of variables */
	unsigned int vlist_len;   /* size of vlist */
	unsigned start;           /* starting line */
	unsigned nested;          /* number of compounds nested below this */
	struct cfg_var **vlist;   /* array of variables */
	struct cfg_comp *parent;  /* nested from */
	struct cfg_comp **nest;   /* compounds nested inside this one */
};

/* prototypes */
extern struct cfg_comp *cfg_parse_file(const char *path);
extern void cfg_destroy_compound(struct cfg_comp *comp);
extern void cfg_warn(struct cfg_comp *comp, struct cfg_var *v, const char *fmt, ...)
	__attribute__((__format__(__printf__, 3, 4)));
extern void cfg_error(struct cfg_comp *comp, struct cfg_var *v, const char *fmt, ...)
	__attribute__((__format__(__printf__, 3, 4), __noreturn__));
#endif /* _CONFIG_H */
