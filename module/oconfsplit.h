#ifndef INCLUDE_oconfsplit_h__
#define INCLUDE_oconfsplit_h__

void split_init(void);
void split_deinit(void);

int split_config(void);
int split_grok_var(const char *var, const char *value);

#endif
