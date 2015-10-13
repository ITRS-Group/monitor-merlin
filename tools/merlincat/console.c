#include "console.h"
#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>

// We only have one buffer, keep it big
#define CONSOLE_IO_BUFSIZE (1024*1024)

struct ConsoleIO_ {
	void (*session_newline)(const char *, gpointer);
	gpointer user_data;

	GInputStream *instream;
	GSource *insource;

	char buffer[CONSOLE_IO_BUFSIZE];
	int bufptr;
};

static gboolean consoleio_recv(GInputStream *stream, gpointer user_data);

ConsoleIO *consoleio_new(void (*session_newline)(const char *, gpointer),
		gpointer user_data) {
	ConsoleIO * cio;

	cio = g_malloc(sizeof(ConsoleIO));
	cio->session_newline = session_newline;
	cio->user_data = user_data;

	cio->bufptr = 0;

	cio->instream = g_unix_input_stream_new(STDIN_FILENO, FALSE);
	cio->insource = g_pollable_input_stream_create_source(
			G_POLLABLE_INPUT_STREAM(cio->instream), NULL);
	g_source_set_callback(cio->insource, (GSourceFunc) consoleio_recv, cio,
			NULL);
	g_source_attach(cio->insource, NULL);

	return cio;
}

void consoleio_destroy(ConsoleIO *cio) {
	if (cio == NULL)
		return;
	g_source_destroy(cio->insource);
	g_object_unref(cio->instream);
	g_free(cio);
}

static gboolean consoleio_recv(GInputStream *stream, gpointer user_data) {
	ConsoleIO *cio = (ConsoleIO *) user_data;
	GError *error = NULL;
	gssize sz;
	int i;
	gboolean found_line;

	sz = g_input_stream_read(stream, cio->buffer + cio->bufptr,
	CONSOLE_IO_BUFSIZE - cio->bufptr,
	NULL, &error);

	if (sz > 0) {
		cio->bufptr += sz;
		cio->buffer[cio->bufptr] = '\0';
		do {
			found_line = FALSE;
			for (i = 0; i < cio->bufptr; i++) {
				if (cio->buffer[i] == '\n') {
					found_line = TRUE;
					cio->buffer[i] = '\0';

					(*cio->session_newline)(cio->buffer, cio->user_data);

					cio->bufptr -= (i + 1);
					memmove(cio->buffer, &(cio->buffer[i + 1]), cio->bufptr);
					break;
				}
			}
		} while (found_line);

		return TRUE; // continue
	}

	if (sz == 0) {
		g_object_unref(stream);
		return FALSE; // remove source
	}

	printf("Some error: %s\n", error->message);
	g_error_free(error);

	g_object_unref(stream);
	return FALSE; // remove source
}
