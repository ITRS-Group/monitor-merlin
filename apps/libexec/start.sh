#!/bin/sh

# Fix object config (does nothing if we're not a poller)
mon oconf poller-fix

service merlind start
service naemon start
