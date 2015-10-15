#ifndef TOOLS_MERLINCAT_CLIENT_GSOURCE_H_
#define TOOLS_MERLINCAT_CLIENT_GSOURCE_H_

#include <glib.h>
#include "conn_info.h"

struct ClientSource_;
typedef struct ClientSource_ ClientSource;

ClientSource *client_source_new(const ConnectionInfo*,
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
	 * Client user storage (passed to conn_new callback)
	 */
	gpointer user_data);
void client_source_destroy(ClientSource *cs);

#endif /* TOOLS_MERLINCAT_CLIENT_GSOURCE_H_ */
