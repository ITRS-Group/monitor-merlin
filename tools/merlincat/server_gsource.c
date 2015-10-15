#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include "server_gsource.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

struct ServerSource_ {
	GSocketService *sockserv;
	gpointer (*session_new)(ConnectionStorage *, gpointer);
	void (*session_newline)(ConnectionStorage *, gpointer, gsize, gpointer);
	void (*session_destroy)(gpointer);
	gpointer user_data;
};

typedef struct ServerSourceStorage_ {
	ServerSource *csock;
	gpointer user_data;
	ConnectionStorage *conn_store;
	GSocket *sock;
} ServerSourceStorage;

static gboolean server_source_new_request(GSocketService *service,
		GSocketConnection *connection, GObject *source_object,
		gpointer user_data);

static gboolean server_source_recv(GSocket *sock, GIOCondition condition,
		gpointer user_data);
static void server_source_disconnect(gpointer user_data);
static void server_source_send(gpointer conn, gconstpointer data, gsize size);

ServerSource *server_source_new(const ConnectionInfo *conn_info,
		gpointer (*session_new)(ConnectionStorage *, gpointer),
		void (*session_newline)(ConnectionStorage *, gpointer, gsize, gpointer),
		void (*session_destroy)(gpointer), gpointer user_data) {
	GSocketAddress *addr;
	ServerSource *csock;
	GInetAddress *inetaddr;

	csock = g_malloc(sizeof(ServerSource));
	csock->session_new = session_new;
	csock->session_newline = session_newline;
	csock->session_destroy = session_destroy;
	csock->user_data = user_data;

	csock->sockserv = g_socket_service_new();
	if (!csock->sockserv) {
		/* Some error */
	}

	if (conn_info->type == UNIX) {
		addr = g_unix_socket_address_new(conn_info->dest_addr);
	} else { /* conn_info->type == TCP */
		/* Create destination address */
		inetaddr = g_inet_address_new_from_string(conn_info->dest_addr);
		addr = g_inet_socket_address_new(inetaddr, conn_info->dest_port);
		g_object_unref((GObject *) inetaddr);
	}

	if (!g_socket_listener_add_address((GSocketListener*) csock->sockserv, addr,
			G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT,
			NULL,
			NULL,
			NULL /* (GError **) */
			)) {
		/* Some error */
	}
	g_object_unref(addr);

	g_signal_connect((GObject * )csock->sockserv, "incoming",
			G_CALLBACK (server_source_new_request), csock);

	return csock;
}

void server_source_destroy(ServerSource *csock) {
	if (csock == NULL)
		return;
	g_socket_listener_close((GSocketListener*) csock->sockserv);
	g_free(csock);
}

static gboolean server_source_new_request(GSocketService *service,
		GSocketConnection *connection, GObject *source_object,
		gpointer user_data) {
	ServerSource *csock = (ServerSource *) user_data;
	GSocket *sock;
	GSource *source;
	ServerSourceStorage *stor;

	sock = g_socket_connection_get_socket(connection);
	g_socket_set_blocking(sock, FALSE);

	stor = g_malloc(sizeof(ServerSourceStorage));
	stor->csock = csock;
	stor->sock = sock;

	source = g_socket_create_source(sock, G_IO_IN, NULL);

	g_source_set_callback(source, (GSourceFunc) server_source_recv, stor,
			server_source_disconnect);
	g_source_attach(source, NULL);

	/*
	 * the ConnectionStorage doesn't own its user_data, but is just a reference
	 * to parent struct, thus no destroy method
	 */
	stor->conn_store = connection_new(server_source_send, NULL, stor);

	stor->user_data = (*csock->session_new)(stor->conn_store, csock->user_data);

	return FALSE;
}

static gboolean server_source_recv(GSocket *sock, GIOCondition condition,
		gpointer user_data) {
	ServerSourceStorage *stor = (ServerSourceStorage *) user_data;
	GError *error = NULL;
	gssize sz;
	int i;
	gboolean found_line;
	gboolean running;
	gchar buffer[8192];

	sz = g_socket_receive(sock, buffer, 8192,
	NULL, &error);

	if (sz > 0) {
		(*stor->csock->session_newline)(stor->conn_store, buffer, sz,
				stor->user_data);
		return TRUE; // TRUE = continue source
	}

	if (sz == 0) {
		g_object_unref(sock);
		return FALSE; // FALSE = remove source
	}

	printf("Some error: %s\n", error->message);
	g_error_free(error);

	g_object_unref(sock);
	return FALSE; // FALSE = remove source
}

static void server_source_disconnect(gpointer user_data) {
	ServerSourceStorage *stor = (ServerSourceStorage *) user_data;
	(*stor->csock->session_destroy)(stor->user_data);
	connection_destroy(stor->conn_store);
	g_free(stor);
}

static void server_source_send(gpointer conn, gconstpointer data, gsize size) {
	ServerSourceStorage *stor = (ServerSourceStorage *) conn;
	g_socket_send(stor->sock, data, size, NULL, NULL);
}
