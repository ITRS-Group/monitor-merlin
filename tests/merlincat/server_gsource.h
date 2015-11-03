#ifndef TOOLS_MERLINCAT_SERVER_GSOURCE_H_
#define TOOLS_MERLINCAT_SERVER_GSOURCE_H_

#include "conn_info.h"

#include <glib.h>

struct ServerSource_;
typedef struct ServerSource_ ServerSource;

ServerSource *server_source_new(const ConnectionInfo *conn_info,
	/*
	 * arg 1: connection reference, for sending data
	 * arg 2: user_data passed to this method
	 * Returns connection specific user data storage
	 */
	gpointer (*conn_new)(ConnectionStorage *, gpointer),

	/*
	 * arg 1: connection reference, for sending data
	 * arg 2: data buffer for receiving data
	 * arg 3: length of recieved data
	 * arg 4: connection user storage
	 */
	void (*conn_data)(ConnectionStorage *, gpointer, gsize, gpointer),

	/*
	 * arg 1: connection user storage
	 */
	void (*conn_close)(gpointer),

	/*
	 * Server user storage (passed to conn_new callback)
	 */
	gpointer user_data);
void server_source_destroy(ServerSource *cs);

#endif /* TOOLS_MERLINCAT_SERVER_GSOURCE_H_ */
