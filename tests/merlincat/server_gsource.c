#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include "server_gsource.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

struct ServerSource_ {
	/* Storage for callbacks to next layer */
	gpointer (*session_new)(ConnectionStorage *, gpointer);
	void (*session_newline)(ConnectionStorage *, gpointer, gsize, gpointer);
	void (*session_destroy)(gpointer);
	gpointer user_data;

	/* Storage of active connections */
	GPtrArray *active_connections;

	/* Unix socket to clean up afterwards */
	gchar *socket_path;

	/* glib-related storage */
	GSocket *listening_socket;
	GSource *listening_source;
};

typedef struct ServerSourceConnection_ {
	/* Reference to parent */
	ServerSource *csock;

	/* Storage for higher level information and integration */
	gpointer user_data;
	ConnectionStorage *conn_store;

	/* Track if this is currently being destroyed, to block recursion */
	gboolean being_destroyed;

	/* Client socket */
	GSocket *sock;
	GSource *socksource;
} ServerSourceConnection;

static gboolean server_source_new_request(GSocket *socket,
	GIOCondition condition, gpointer user_data);

static gboolean server_source_recv(GSocket *sock, GIOCondition condition,
	gpointer user_data);
static void server_source_disconnect(gpointer user_data);
static void server_source_conn_free(gpointer user_data);
static void server_source_send(gpointer conn, gconstpointer data, gsize size);
static gboolean server_source_is_connected(gpointer conn);

ServerSource *server_source_new(const ConnectionInfo *conn_info,
	gpointer (*session_new)(ConnectionStorage *, gpointer),
	void (*session_newline)(ConnectionStorage *, gpointer, gsize, gpointer),
	void (*session_destroy)(gpointer), gpointer user_data) {
	GSocketAddress *addr;
	ServerSource *csock;
	GError *error = NULL;
	GSource *listen_source;

	csock = g_malloc0(sizeof(ServerSource));
	/* Update callbacks */
	csock->session_new = session_new;
	csock->session_newline = session_newline;
	csock->session_destroy = session_destroy;
	csock->user_data = user_data;

	/* Create storage for active connections */
	csock->active_connections = g_ptr_array_new_with_free_func(
		server_source_conn_free);

	/* Get bind address for socket */
	if (conn_info->type == UNIX) {
		csock->socket_path = g_strdup(conn_info->dest_addr);
		addr = g_unix_socket_address_new(csock->socket_path);
	} else { /* conn_info->type == TCP */
		GInetAddress *inetaddr = g_inet_address_new_from_string(
			conn_info->dest_addr);
		addr = g_inet_socket_address_new(inetaddr, conn_info->dest_port);
		g_object_unref(G_OBJECT(inetaddr));
	}

	/* Create non-blocking listening socket */
	csock->listening_socket = g_socket_new(g_socket_address_get_family(addr),
		G_SOCKET_TYPE_STREAM, 0, &error);
	if (csock->listening_socket == NULL) {
		g_printerr("Can't create socket: %s\n", error->message);
		g_error_free(error);
		server_source_destroy(csock);
		return NULL;
	}
	g_socket_set_blocking(csock->listening_socket, FALSE);

	/* Bind socket to address */
	if (!g_socket_bind(csock->listening_socket, addr, FALSE, &error)) {
		g_printerr("Can't bind socket: %s\n", error->message);
		g_error_free(error);
		g_object_unref(addr);
		server_source_destroy(csock);
		return NULL;
	}

	/* Free up address, it's already bound to socket */
	g_object_unref(addr);

	/* Start listening to socket, so it starts accepting connections */
	if (!g_socket_listen(csock->listening_socket, &error)) {
		g_printerr("Can't listen on socket: %s\n", error->message);
		g_error_free(error);
		server_source_destroy(csock);
		return NULL;
	}

	/* Add callbacks for new connections */
	listen_source = g_socket_create_source(csock->listening_socket,
		G_IO_IN, NULL);
	g_source_set_callback(listen_source, (GSourceFunc)server_source_new_request, csock,
		server_source_disconnect);
	g_source_attach(listen_source, NULL);
	g_source_unref(listen_source);

	return csock;
}

void server_source_destroy(ServerSource *csock) {
	GError *error = NULL;

	if (csock == NULL)
		return;

	/* Clean up unix socket, if available */
	if (csock->socket_path) {
		unlink(csock->socket_path);
		g_free(csock->socket_path);
	}

	/* Close all connections before continue */
	g_ptr_array_unref(csock->active_connections);

	/* Close and destroy listening socket */
	if (!g_socket_close(csock->listening_socket, &error)) {
		g_printerr("Can not close listening socket: %s\n", error->message);
		g_error_free(error);
		error = NULL;
	}

	g_free(csock);
}

static gboolean server_source_new_request(GSocket *socket,
	GIOCondition condition, gpointer user_data) {

	ServerSource *csock = (ServerSource *) user_data;
	ServerSourceConnection *stor;
	GError *error = NULL;

	stor = g_malloc0(sizeof(ServerSourceConnection));
	stor->csock = csock;

	stor->sock = g_socket_accept(socket, NULL, &error);
	if (!stor->sock) {
		g_printerr("Error accepting socket: %s\n", error->message);
		g_error_free(error);
		g_free(stor);
		return FALSE;
	}

	stor->conn_store = connection_new(server_source_send, NULL,
		server_source_is_connected, stor);

	g_socket_set_blocking(stor->sock, FALSE);
	stor->socksource = g_socket_create_source(stor->sock, G_IO_IN, NULL);

	g_source_set_callback(stor->socksource, (GSourceFunc) server_source_recv,
		stor, server_source_disconnect);
	g_source_attach(stor->socksource, NULL);

	stor->user_data = (*stor->csock->session_new)(stor->conn_store,
		stor->csock->user_data);

	g_ptr_array_add(csock->active_connections, stor);

	return FALSE;
}

static gboolean server_source_recv(GSocket *sock, GIOCondition condition,
	gpointer user_data) {
	ServerSourceConnection *stor = (ServerSourceConnection *) user_data;
	GError *error = NULL;
	gssize sz;
	gchar buffer[8192];

	sz = g_socket_receive(sock, buffer, 8192,
	NULL, &error);

	if (sz > 0) {
		(*stor->csock->session_newline)(stor->conn_store, buffer, sz,
			stor->user_data);
		return TRUE; // TRUE = continue source
	}

	if (sz == 0) {
		return FALSE; // FALSE = remove source
	}

	fprintf(stderr, "Error reading from socket: %s\n", error->message);
	g_error_free(error);

	return FALSE; // FALSE = remove source
}

static void server_source_disconnect(gpointer user_data) {
	ServerSourceConnection *stor = (ServerSourceConnection *) user_data;

	/*
	 * This method can be called if by g_source_destroy below implicitly, and
	 * we shouldn't call this twice, so block.
	 */
	if (stor->being_destroyed)
		return;

	g_ptr_array_remove_fast(stor->csock->active_connections, stor);
}

static void server_source_conn_free(gpointer user_data) {
	ServerSourceConnection *stor = (ServerSourceConnection *) user_data;
	GError *error;

	/* Block recursion into this method through g_source_destroy */
	stor->being_destroyed = TRUE;

	(*stor->csock->session_destroy)(stor->user_data);

	g_source_destroy(stor->socksource);

	connection_destroy(stor->conn_store);

	/* Close and destroy socket */
	if (!g_socket_close(stor->sock, &error)) {
		g_printerr("Can't close client socket: %s\n", error->message);
		g_error_free(error);
		error = NULL;
	}

	g_free(stor);
}

static void server_source_send(gpointer conn, gconstpointer data, gsize size) {
	ServerSourceConnection *stor = (ServerSourceConnection *) conn;
	g_socket_send(stor->sock, data, size, NULL,
	NULL);
}

static gboolean server_source_is_connected(gpointer conn) {
	ServerSourceConnection *stor = (ServerSourceConnection *) conn;
	return g_socket_is_connected(stor->sock);
}
