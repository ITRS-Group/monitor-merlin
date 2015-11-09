#include "conn_info.h"

struct ConnectionStorage_ {
	void (*send)(gpointer, gconstpointer, gsize);
	void (*destroy)(gpointer);
	gboolean (*is_connected)(gpointer);
	gpointer user_data;
};

ConnectionStorage *connection_new(
		void (*send)(gpointer, gconstpointer, gsize),
		void (*destroy)(gpointer),
		gboolean (*is_connected)(gpointer),
		gpointer user_data) {
	ConnectionStorage *cs = g_malloc(sizeof(ConnectionStorage));
	if(cs == NULL)
		return NULL;
	cs->send = send;
	cs->destroy = destroy;
	cs->is_connected = is_connected;
	cs->user_data = user_data;
	return cs;
}

void connection_destroy(ConnectionStorage *cs) {
	if(cs == NULL)
		return;
	if(cs->destroy)
		(*cs->destroy)(cs->user_data);
	g_free(cs);
}

void connection_send(ConnectionStorage *cs, gconstpointer data, gsize len) {
	(*cs->send)(cs->user_data, data, len);
}

gboolean connection_is_connected(ConnectionStorage *cs) {
	return (*cs->is_connected)(cs->user_data);
}
