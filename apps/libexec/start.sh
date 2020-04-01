#!/bin/sh

# Fix object config (does nothing if we're not a poller)
mon oconf poller-fix

/bin/systemctl start merlind
/bin/systemctl start naemon
if systemctl list-units --full -all | grep -Fq "op5-monitor.service"; then
    /bin/systemctl start op5-monitor
fi
