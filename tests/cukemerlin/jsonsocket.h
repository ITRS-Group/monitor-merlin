#ifndef JSONSOCKET_H
#define JSONSOCKET_H

#include <glib.h>
#include <gio/gio.h>

#include "json.h"

struct JSONSocket_;
typedef struct JSONSocket_ JSONSocket;

JSONSocket *jsonsocket_new(const gchar *bind_addr, const gint bind_port,
		gpointer (*session_new)(GSocket *, gpointer),
		gboolean (*session_newline)(GSocket *, JsonNode *, gpointer),
		void (*session_destroy)(gpointer), gpointer user_data);
void jsonsocket_destroy(JSONSocket *sockserv);

void jsonsocket_send(GSocket *sock, const JsonNode *node);

#endif
