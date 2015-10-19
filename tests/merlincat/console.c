#include "console.h"
#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>

/*
 * Change this to 1, and then remove the code that is masked by #if-statements
 * when we can use a more modern glib2 (2.28) than 4y (today is october 2015)
 */
#define USE_G_POLLABLE_INPUT_STREAM 0

#if !USE_G_POLLABLE_INPUT_STREAM
#include <fcntl.h>
#endif

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

#if !USE_G_POLLABLE_INPUT_STREAM

typedef struct ConsoleIOSource_ {
	GSource source;
	GPollFD pollfd;
	ConsoleIO *cio;
} ConsoleIOSource;

static gboolean consoleio_source_prepare(GSource *source, gint *timeout_);
static gboolean consoleio_source_check(GSource *source);
static gboolean consoleio_source_dispatch(GSource *source, GSourceFunc callback,
		gpointer user_data);
static void consoleio_source_finalize(GSource *source);

static GSourceFuncs consoleio_source_funcs = { .prepare =
		consoleio_source_prepare, .check = consoleio_source_check, .dispatch =
		consoleio_source_dispatch, .finalize = consoleio_source_finalize };
#endif

ConsoleIO *consoleio_new(void (*session_newline)(const char *, gpointer),
		gpointer user_data) {
	ConsoleIO * cio;

	cio = g_malloc(sizeof(ConsoleIO));
	cio->session_newline = session_newline;
	cio->user_data = user_data;

	cio->bufptr = 0;

	cio->instream = g_unix_input_stream_new(STDIN_FILENO, FALSE);
#if USE_G_POLLABLE_INPUT_STREAM
	cio->insource = g_pollable_input_stream_create_source(
			G_POLLABLE_INPUT_STREAM(cio->instream), NULL);
#else

	{ /* Set nonblocking */
		ConsoleIOSource *source;
		gint fd = g_unix_input_stream_get_fd(G_UNIX_INPUT_STREAM(cio->instream));

		int flags = fcntl(fd, F_GETFL, 0);
		if (flags == -1) {
			fprintf(stderr, "Could nog get console fd flags, exiting\n");
			consoleio_destroy(cio);
			return NULL;
		}
		flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
		if (flags == -1) {
			fprintf(stderr, "Could nog set console fd flags, exiting\n");
			consoleio_destroy(cio);
			return NULL;
		}

		source = (ConsoleIOSource*)g_source_new(&consoleio_source_funcs, sizeof(ConsoleIOSource));

		source->pollfd.fd = fd;
		source->pollfd.events = G_IO_IN;
		source->cio = cio;
		cio->insource = (GSource *)source;
		g_source_add_poll(cio->insource, &source->pollfd);
	}
#endif
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
	// Don't use stream, use cio->instream instead, so it's compatible with
	// non gpollableinputstream too
	ConsoleIO *cio = (ConsoleIO *) user_data;
	GError *error = NULL;
	gssize sz;
	int i;
	gboolean found_line;

	sz = g_input_stream_read(cio->instream, cio->buffer + cio->bufptr,
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
		g_object_unref(cio->instream);
		return FALSE; // remove source
	}

	fprintf(stderr, "Error when reading data from console: %s\n",
			error->message);
	g_error_free(error);

	g_object_unref(cio->instream);
	return FALSE; // remove source
}

#if !USE_G_POLLABLE_INPUT_STREAM
static gboolean consoleio_source_prepare(GSource *source, gint *timeout_) {
	return FALSE; // no match
}
static gboolean consoleio_source_check(GSource *source) {
	ConsoleIOSource *src = (ConsoleIOSource *)source;
	return src->pollfd.revents & G_IO_IN; // Only dispatch if IN-event
}
static gboolean consoleio_source_dispatch(GSource *source, GSourceFunc callback,
		gpointer user_data) {
	gboolean (*cb)(GInputStream *, gpointer);
	cb = (gboolean (*)(GInputStream *, gpointer))callback;
	return (*cb)(NULL, user_data);
}
static void consoleio_source_finalize(GSource *source) {

}
#endif
