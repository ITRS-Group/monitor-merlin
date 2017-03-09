#include <glib.h>
#include <gio/gio.h>
#include <stdio.h>
#include <gio/gunixsocketaddress.h>
#include "client_gsource.h"

struct ClientSource_ {
	gpointer (*conn_new)(ConnectionStorage *, gpointer);
	void (*conn_data)(ConnectionStorage *, gpointer, gsize, gpointer);
	void (*conn_close)(gpointer);
	gpointer user_data;

	GSocket *sock;
	gpointer conn_user_data;

	ConnectionStorage *conn_store;

	GSource *source;
};

static gboolean client_source_data_callback(GSocket *socket,
		GIOCondition condition, gpointer user_data);
static void client_source_send(gpointer conn, gconstpointer data, gsize size);
static gboolean client_source_is_connected(gpointer conn);

ClientSource *client_source_new(const ConnectionInfo* conn_info,
	gpointer (*conn_new)(ConnectionStorage *, gpointer),
	void (*conn_data)(ConnectionStorage *, gpointer, gsize, gpointer),
	void (*conn_close)(gpointer),
	gpointer user_data)
{

	GSocketAddress *addr = NULL;
	GInetAddress *inetaddr = NULL;

	ClientSource *cs = g_malloc0(sizeof(ClientSource));
	cs->user_data = user_data;
	cs->sock = NULL;
	cs->source = NULL;

	/*
	 * the ConnectionStorage doesn't own its user_data, but is just a reference
	 * to parent struct, thus no destroy method
	 */
	cs->conn_store = connection_new(client_source_send, NULL, client_source_is_connected, cs);

	if (conn_info->type == UNIX) {
		cs->sock = g_socket_new(G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, NULL);
                g_assert(cs->sock != NULL);

		addr = g_unix_socket_address_new(conn_info->dest_addr);
	} else { /* conn_info->type == TCP */
		cs->sock = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, NULL);
                g_assert(cs->sock != NULL);

		/* Try to bind with a given source address... FIXME */
		inetaddr = g_inet_address_new_from_string(conn_info->source_addr);
                g_assert(inetaddr != NULL);
		addr = g_inet_socket_address_new(inetaddr, conn_info->source_port);
		g_assert(g_socket_bind(cs->sock, addr, TRUE, NULL) == TRUE);
		g_object_unref((GObject *)addr);
		g_object_unref((GObject *)inetaddr);

		/* Create destination address */
		inetaddr = g_inet_address_new_from_string(conn_info->dest_addr);
                g_assert(inetaddr != NULL);
		addr = g_inet_socket_address_new(inetaddr, conn_info->dest_port);
		g_object_unref((GObject *)inetaddr);
	}

	if(!g_socket_connect(cs->sock, addr, NULL, NULL)) {
		g_object_unref((GObject *)addr);
		if (inetaddr) g_object_unref((GObject *)inetaddr);
		client_source_destroy(cs);
		return NULL;
	}
	g_object_unref((GObject *)addr);

	/*
	 * These must be set just before conn_new is called, otherwise cleanup might
	 * call conn_close without conn_new called, if cs is destroyed prematurely
	 */
	cs->conn_new = conn_new;
	cs->conn_data = conn_data;
	cs->conn_close = conn_close;
	cs->conn_user_data = (*cs->conn_new)(cs->conn_store, cs->user_data);

	cs->source = g_socket_create_source(cs->sock, G_IO_IN, NULL);
	g_source_set_callback(cs->source, (GSourceFunc)client_source_data_callback, (gpointer)cs, NULL);
	g_source_attach(cs->source, NULL);

	return cs;
}

static gboolean client_source_data_callback(GSocket *socket,
		GIOCondition condition, gpointer user_data) {
	ClientSource *cs = (ClientSource *)user_data;
	gchar buffer[8192];
	gssize size;

	size = g_socket_receive(socket, buffer, 8192, NULL, NULL);
	if(size > 0) {
		// Got data
		(*cs->conn_data)(cs->conn_store, buffer, size, cs->conn_user_data);
		return TRUE;
	}

        g_print("INFO: inside client_source_data_callback\n");
	if(size == 0) {
                g_print("INFO: connection was closed by peer\n");
		// Connection closed
		if(cs->conn_close) {
			(*cs->conn_close)(cs->conn_user_data);
			/* We called the close method for this connection, mark it as we shouldn't call it again */
			cs->conn_close = NULL;
		}

		g_socket_close(cs->sock, NULL);
		g_object_unref(cs->sock);
		cs->sock = NULL;

		cs->source = NULL;
		return FALSE; // FALSE = remove source
	}

	// Error
	cs->source = NULL;
	return FALSE; // FALSE = remove source
}

void client_source_destroy(ClientSource *cs) {
	if(cs == NULL)
		return;
        g_print("INFO: inside client_source_destroy\n");


	if(cs->sock) {
		g_socket_close(cs->sock, NULL);
		g_object_unref(cs->sock);
	}
	if(cs->source) {
		g_source_destroy(cs->source);
	}
	// Make sure destructor is called exactly once
	if(cs->conn_close) {
		(*cs->conn_close)(cs->conn_user_data);
		/* We called the close method for this connection, mark it as we shouldn't call it again */
		cs->conn_close = NULL;
	}
	if(cs->conn_store) {
		connection_destroy(cs->conn_store);
	}
	g_free(cs);
}

static void client_source_send(gpointer conn, gconstpointer data, gsize size) {
	ClientSource *cs = (ClientSource *)conn;
	g_socket_send(cs->sock, data, size, NULL, NULL);
}


static gboolean client_source_is_connected(gpointer conn) {
	ClientSource *cs = (ClientSource *)conn;
	return g_socket_is_connected(cs->sock);
}
