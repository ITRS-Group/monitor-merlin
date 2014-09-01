#ifndef INCLUDE_state_h__
#define INCLUDE_state_h__
extern int state_init(void);
extern int host_has_new_state(char *host, int state, int type);
extern int service_has_new_state(char *host, char *desc, int state, int type);
#define CAT_STATE(__state, __type) ((__state | (__type << 8)))
#endif
