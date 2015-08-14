#ifndef INCLUDE_misc_h__
#define INCLUDE_misc_h__

#include <time.h>
#include "module.h"
#include "sha1.h"

void file_list_free(struct file_list *list);
time_t get_last_cfg_change(void);
file_list **get_sorted_oconf_files(unsigned int *n_files);
int get_config_hash(unsigned char *hash);
int hash_add_file(const char *path, blk_SHA_CTX *ctx);

#endif
