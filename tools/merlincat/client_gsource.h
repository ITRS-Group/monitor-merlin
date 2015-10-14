#ifndef TOOLS_MERLINCAT_CLIENT_GSOURCE_H_
#define TOOLS_MERLINCAT_CLIENT_GSOURCE_H_

#include <glib.h>

struct ClientSource_;
typedef struct ClientSource_ ClientSource;

struct ConnectionInfo {
	 enum { TCP, UNIX } type;
	 char *dest_addr;
	 int dest_port;
	 char *source_addr;
	 int source_port;
	 gboolean listen;
};

ClientSource *client_source_new(const struct ConnectionInfo*,
	/*
	 * arg 1: connection reference, for sending data
	 * arg 2: user_data passed to this method
	 * Returns connection specific user data storage
	 */
	gpointer (*conn_new)(gpointer, gpointer),

	/*
	 * arg 1: connection reference, for sending data
	 * arg 2: data buffer for receiving data
	 * arg 3: length of recieved data
	 * arg 4: connection user storage
	 */
	void (*conn_data)(gpointer, gpointer, gsize, gpointer),

	/*
	 * arg 1: connection user storage
	 */
	void (*conn_close)(gpointer),

	/*
	 * Client user storage (passed to conn_new callback)
	 */
	gpointer user_data);
void client_source_destroy(ClientSource *cs);

void client_source_send(gpointer conn, gpointer data, glong size);

#endif /* TOOLS_MERLINCAT_CLIENT_GSOURCE_H_ */
