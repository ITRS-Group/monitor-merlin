#ifndef TESTS_CUKEMERLIN_CUKESOCKET_H_
#define TESTS_CUKEMERLIN_CUKESOCKET_H_

struct CukeSocket_;
typedef struct CukeSocket_ CukeSocket;

CukeSocket *cukesock_new(const gchar *bind_addr, const gint bind_port);
void cukesock_destroy(CukeSocket *cs);


#endif /* TESTS_CUKEMERLIN_CUKESOCKET_H_ */
