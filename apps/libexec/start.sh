#!/bin/sh

# Fix object config (does nothing if we're not a poller)
mon oconf poller-fix

/sbin/service merlind start
/sbin/service naemon start
