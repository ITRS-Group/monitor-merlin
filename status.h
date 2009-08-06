#ifndef INCLUDE_status_h__
#define INCLUDE_status_h__

struct object_state {
	char *name;
	int state;
};
typedef struct object_state object_state;

/* macros used on the 'state' member of the object_state struct */
#define extract_state(state) (state & 0xffff)
#define extract_type(state) ((state >> 16) & 1)
#define concat_state(type, state) ((type << 16) | state)

extern int prime_object_states(size_t *hosts, size_t *services);
extern object_state *get_host_state(const char *name);
extern object_state *get_service_state(const char *h_name, const char *s_name);
extern object_state *get_object_state(const char *name, size_t id);
extern size_t foreach_state(int id, int (*fn)(object_state *));
#endif /* INCLUDE_status_h__ */
