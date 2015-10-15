/*
 * merlinreader.h
 *
 * This file reimplements the merlin packet encoder/decoder. This version should
 * be treated as a reference implementation for testing purposes, so changes in
 * merlin which introduces possible bugs shouldn't affect the test code.
 *
 * So don't change this file unless possible SIGSEGV or similar in the tests.
 */

#ifndef TOOLS_MERLINCAT_MERLINREADER_H_
#define TOOLS_MERLINCAT_MERLINREADER_H_

#include <glib.h>
#include <shared/node.h>

struct MerlinReader_;
typedef struct MerlinReader_ MerlinReader;

MerlinReader *merlinreader_new(void);
void merlinreader_destroy(MerlinReader *mr);

/**
 * Add data to the merlin reader buffer.
 *
 * Returns number of bytes used from block refered to as "data" (of size "size")
 */
gsize merlinreader_add_data(MerlinReader *mr, gpointer data, gsize size);
merlin_event *merlinreader_get_event(MerlinReader *mr);

#endif /* TOOLS_MERLINCAT_MERLINREADER_H_ */
