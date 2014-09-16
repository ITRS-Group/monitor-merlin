#include "module.h"
#include "sha1.h"
#include <sys/mman.h>
#include <naemon/naemon.h>
#include <libgen.h>

/* does a deep free of a file_list struct */
void file_list_free(struct file_list *list)
{
	int i = 0;

	file_list *p;

	while ((p = list)) {
		i++;
		free(list->name);
		list = list->next;
		free(p);
	}
}


/*
 * Reads a file named *fname into a buffer and zero-separates its
 * lines into a pretty neat array
 */
static char *read_strip_split(char *fname, int *size)
{
	char *buf, *p;
	int fd, foo;
	int i;
	struct stat st = {0,};

	if (!fname)
		return NULL;

	if (stat(fname, &st) < 0)
		return NULL;

	fd = open(fname, O_RDONLY);
	if (fd < 0)
		return NULL;

	if (!(buf = malloc(st.st_size))) {
		close(fd);
		return NULL;
	}
	*size = st.st_size;

	foo = read(fd, buf, st.st_size);
	close(fd);
	if (foo == -1) {
		free(buf);
		return NULL;
	}

	for (i = 0; i < st.st_size; i++) {
		if (buf[i] == '\n') {
			p = &buf[i];
			while (isspace(*p)) {
				*p = 0;
				p--;
			}
			buf[i] = '\0';
		}
	}

	return buf;
}


/*
 * recursive function to scan for .cfg files.
 * the static variable depth makes sure we don't loop for ever in
 * a nested symlink tree
 */
static struct file_list *recurse_cfg_dir(char *path, struct file_list *list,
					  int max_depth, int depth)
{
	DIR *dp;
	struct dirent *df;
	char cwd[PATH_MAX], wd[PATH_MAX];	/* current working directory */
	size_t wdl;					/* length of current working directory */
	struct stat st;

	memset(cwd, 0, sizeof(cwd));
	memset(wd, 0, sizeof(wd));
	getcwd(cwd, sizeof(cwd));

	dp = opendir(path);
	if (!dp || chdir(path) < 0) {
		chdir(cwd);
		return NULL;
	}

	depth++;

	getcwd(wd, sizeof(wd));
	wdl = strlen(wd);

	while ((df = readdir(dp))) {
		unsigned len;
		struct file_list *fl;

		if (!df->d_name)
			continue;
		if (!strcmp(df->d_name, ".") || !strcmp(df->d_name, ".."))
			continue;
		stat(df->d_name, &st);

		if (S_ISDIR(st.st_mode)) {
			/* don't recurse for ever */
			if (depth >= max_depth)
				continue;
			list = recurse_cfg_dir(df->d_name, list, max_depth, depth + 1);	/* parse recursively */
			continue;
		}

		/* we only want files and symlinks (stat() will follow symlinks) */
		if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode))
			continue;
		len = strlen(df->d_name);
		if (len < 4 || (strcmp(&df->d_name[len - 4], ".cfg")))
			continue;

		/* found a file matching "*.cfg" pattern */
		fl = malloc(sizeof(*fl));
		fl->next = list;
		list = fl;

		list->name = malloc(wdl + len + 2);
		memcpy(&list->st, &st, sizeof(struct stat));
		sprintf(list->name, "%s/%s", wd, df->d_name);
	}

	closedir(dp);
	chdir(cwd);

	return list;
}

/* fetches object configuration files in a nagios-compatible fashion
 * (reads cfg_file= variables and enters and recurses cfg_dir= statements,
 * through the use of the recursive function recurse_cfg_dir) */
static struct file_list *get_cfg_files(char *str, struct file_list *list)
{
	char *p, *base_path = NULL;
	int size, i;

	if (!str)
		return list;

	base_path = dirname(nspath_real(str, NULL));

	p = read_strip_split(str, &size);
	if (!p || !size)
		return NULL;

	for (i = 0; i < size; i += strlen(&p[i]) + 1) {
		if (!prefixcmp(&p[i], "cfg_file=")) {
			struct file_list *fl;

			i += 9;

			/*
			 * get a new list entry and point its tail
			 * to the previous one. If the previous one
			 * was NULL, that means its our sentinel
			 */
			fl = malloc(sizeof(*fl));
			if (!fl)
				return list;
			fl->next = list;
			list = fl;

			list->name = nspath_absolute(&p[i], base_path);
			if (!list->name)
				return list;
			stat(list->name, &list->st);
		}
		else if (!prefixcmp(&p[i], "cfg_dir=")) {
			char *dir;
			i += 8;
			dir = nspath_absolute(&p[i], base_path);
			list = recurse_cfg_dir(dir, list, 4, 0);
			free(dir);
		}
	}

	free(p);
	free(base_path);

	return list;
}

/* returns the last timestamp of a configuration change */
time_t get_last_cfg_change(void)
{
	time_t mt = 0;
	struct file_list *flist, *base;

	base = flist = get_cfg_files(config_file, NULL);

	for (; flist; flist = flist->next) {
		if (flist->st.st_mtime > mt)
			mt = flist->st.st_mtime;
	}

	if (base)
		file_list_free(base);

	/* 0 if we for some reason failed */
	return mt;
}

static int flist_cmp(const void *a_, const void *b_)
{
	const file_list *a = *(const file_list **)a_;
	const file_list *b = *(const file_list **)b_;

	return strcmp(a->name, b->name);
}

/* mmap() the file and hash its contents, adding it to ctx */
static int flist_hash_add(struct file_list *fl, blk_SHA_CTX *ctx)
{
	void *map;
	int fd;

	fd = open(fl->name, O_RDONLY);
	if (fd < 0)
		return -1;
	map = mmap(NULL, fl->st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (!map)
		return -1;
	blk_SHA1_Update(ctx, map, fl->st.st_size);
	munmap(map, fl->st.st_size);
	close(fd);

	return 0;
}

file_list **get_sorted_oconf_files(unsigned int *n_files)
{
	struct file_list *flist, *base, **sorted_flist;
	unsigned int i = 0, num_files = 0;

	if (!(base = get_cfg_files(config_file, NULL)))
		return NULL;

	/*
	 * this is horribly inefficient, but I don't really care.
	 * it works, and it was quick to write up. I'll improve on
	 * it later.
	 */
	for (flist = base; flist; flist = flist->next) {
		num_files++;
	}

	sorted_flist = calloc(num_files, sizeof(file_list *));
	for (flist = base; flist; flist = flist->next) {
		sorted_flist[i++] = flist;
	}
	qsort(sorted_flist, num_files, sizeof(file_list *), flist_cmp);

	*n_files = num_files;

	return sorted_flist;
}

/*
 * calculate a sha1 hash of the contents of all config files
 * sorted by their full path.
 * *hash must hold at least 20 bytes
 */
int get_config_hash(unsigned char *hash)
{
	struct file_list **sorted_flist;
	unsigned int num_files = 0, i = 0;
	blk_SHA_CTX ctx;

	blk_SHA1_Init(&ctx);

	sorted_flist = get_sorted_oconf_files(&num_files);

	for (i = 0; i < num_files; i++) {
		flist_hash_add(sorted_flist[i], &ctx);
		sorted_flist[i]->next = NULL;
		file_list_free(sorted_flist[i]);
	}
	blk_SHA1_Final(hash, &ctx);
	free(sorted_flist);

	return 0;
}
