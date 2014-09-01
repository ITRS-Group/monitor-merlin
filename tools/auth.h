#ifndef INCLUDE_auth_h__
#define INCLUDE_auth_h__
int auth_host_ok(const char *host);
int auth_service_ok(const char *host, const char *service);
int auth_read_input(FILE *input);
#endif
