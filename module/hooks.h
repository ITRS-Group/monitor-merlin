#ifndef INCLUDE_hooks_h__
#define INCLUDE_hooks_h__

#include <inttypes.h>
#include <naemon/naemon.h>

neb_cb_result * merlin_mod_hook(int cb, void *data);
extern void *neb_handle;
extern int merlin_hooks_init(uint32_t mask);
extern int merlin_hooks_deinit(void);
extern void merlin_set_block_comment(nebstruct_comment_data *cmnt);

#endif
