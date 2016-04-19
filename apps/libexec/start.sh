#!/bin/sh

# Fix object config (does nothing if we're not a poller)
mon oconf poller-fix

/etc/init.d/merlind start
/etc/init.d/monitor start
