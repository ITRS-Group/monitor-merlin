#ifndef INCLUDE_merlincat_codec_h__
#define INCLUDE_merlincat_codec_h__

/**
 * NOTE: This is a version of codec.h, used as a reference implementation of
 * codecs.
 *
 * This implementation should not be changed, unless there is a explicit SIGSEGV
 * or similar to resolve within the test code. Changing this means that the
 * protocol have changed, and regression bugs can appear
 */

#include <glib.h>

int merlincat_encode(gpointer data, int cb_type, gpointer buf, gsize buflen);
int merlincat_decode(gpointer ds, gsize len, int cb_type);

#endif
