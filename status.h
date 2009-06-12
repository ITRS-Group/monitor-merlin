#ifndef status_h__
#define status_h__

struct object_state {
	char *name;
	int state;
};
typedef struct object_state object_state;

extern int prime_object_states(size_t *hosts, size_t *services);
extern object_state *get_host_state(const char *name);
extern object_state *get_service_state(const char *h_name, const char *s_name);
extern object_state *get_object_state(const char *name, size_t id);
extern size_t foreach_state(int id, int (*fn)(object_state *));
#endif /* status_h__ */
