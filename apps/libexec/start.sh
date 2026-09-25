#!/bin/sh

# Fix object config (does nothing if we're not a poller)
mon oconf poller-fix

/bin/systemctl start merlind
/bin/systemctl start naemon
if [ "$(systemctl show -p LoadState --value op5-monitor.service 2>/dev/null)" = "loaded" ]; then
    /bin/systemctl start op5-monitor
fi
