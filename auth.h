#ifndef INCLUDE_auth_h__
#define INCLUDE_auth_h__
extern int auth_host_ok(const char *host);
extern int auth_service_ok(const char *host, const char *service);
extern void auth_set_user(const char *username);
extern void auth_parse_permission(const char *key, const char *value);
extern const char *auth_get_user(void);
extern void auth_parse_perm(const char *key, const char *value);
extern int auth_init(const char *path);
extern void auth_deinit(void);
#endif
