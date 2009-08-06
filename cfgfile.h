#ifndef INCLUDE_cfgfile_h__
#define INCLUDE_cfgfile_h__

/* have a care with this one. It can't be used when (c) has side-effects */
#undef ISSPACE
#define ISSPACE(c) ((c) == ' ' || (c) == '\t')

/* types */
struct cfg_var {
	unsigned line; /* starting line */
	char *key;
	char *value;
	size_t key_len;
	size_t value_len;
};

#define next_var(c) (c->cur_var >= c->vars ? NULL : c->vlist[c->cur_var++])

struct cfg_comp {
	const char *name;         /* filename, or everything leading up to '{' */
	char *buf;                /* file contents, if this is a file */
	char *template_name;      /* 'name' entry from compound */
	char *use;                /* the value of any "use" variables */
	unsigned int vars;        /* number of variables */
	unsigned int cur_var;     /* current variable (for next_var) */
	unsigned int vlist_len;   /* size of vlist */
	unsigned start, end;      /* starting and ending line */
	unsigned nested;          /* number of compounds nested below this */
	struct cfg_var **vlist;   /* array of variables */
	struct cfg_comp *parent;  /* nested from */
	struct cfg_comp **nest;   /* compounds nested inside this one */
};

/* prototypes */
struct cfg_comp *cfg_parse_file(const char *path);
void cfg_destroy_compound(struct cfg_comp *comp);
void cfg_warn(struct cfg_comp *comp, struct cfg_var *v, const char *fmt, ...)
	__attribute__((__format__(__printf__, 3, 4)));
void cfg_error(struct cfg_comp *comp, struct cfg_var *v, const char *fmt, ...)
	__attribute__((__format__(__printf__, 3, 4), __noreturn__));
char *cfg_copy_value(struct cfg_var *v);

#endif /* _CONFIG_H */
