/*
 * conn_info.h
 *
 *  Created on: Oct 14, 2015
 *      Author: msikstrom
 */

#ifndef TOOLS_MERLINCAT_CONN_INFO_H_
#define TOOLS_MERLINCAT_CONN_INFO_H_

#include <glib.h>

/**
 * Description of connection information, used as bundling arguments to both
 * client_source_new and server_source_new
 *
 * No methods taking this struct as an argumnet are allowed to keep a reference
 * to this struct, but only use it as named attributes to the method called.
 */
typedef struct ConnectionInfo_ {
	 enum { TCP, UNIX } type;
	 char *dest_addr;
	 int dest_port;
	 char *source_addr;
	 int source_port;
	 gboolean listen;
} ConnectionInfo;

/**
 * Information about a connection.
 */
struct ConnectionStorage_;
typedef struct ConnectionStorage_ ConnectionStorage;

ConnectionStorage *connection_new(
		void (*send)(gpointer, gconstpointer, gsize),
		void (*destroy)(gpointer),
		gpointer user_data);
void connection_destroy(ConnectionStorage *cs);
void connection_send(ConnectionStorage *cs, gconstpointer data, gsize len);

#endif /* TOOLS_MERLINCAT_CONN_INFO_H_ */
