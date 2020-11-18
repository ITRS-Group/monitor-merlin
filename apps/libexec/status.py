from __future__ import print_function
import os, sys
import time

import merlin_db


def cmd_install_time(args):
    """
    Print the time the system first recorded an event, as a unix timestamp.

    If no event is recorded, assume an event will happen quite soon, so print
    the current tiemstamp instead
    """
    starttime = int(time.time())

    dbc = merlin_db.connect(mconf).cursor()

    dbc.execute("SELECT MIN(timestamp) FROM report_data")
    for (tstamp,) in dbc:
        if tstamp:
            starttime = tstamp

    merlin_db.disconnect()

    print(starttime)
