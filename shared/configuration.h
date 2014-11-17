/*
 * Common configuration definitions between module and daemon
 */

#ifndef INCLUDE_configuration_h__
#define INCLUDE_configuration_h__

#include "node.h"
#include "cfgfile.h"

int grok_confsync_compound(struct cfg_comp *comp, merlin_confsync *csync);
int grok_common_var(struct cfg_comp *config, struct cfg_var *v);

#endif
