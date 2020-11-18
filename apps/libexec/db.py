import os.path, glob, sys

try:
    from hashlib import sha1
except ImportError:
    from sha import sha as sha1

import merlin_db

log_file = "/tmp/merlin-sql-upgrade.log"


def cmd_fixindexes(args):
    """
    Fixes indexes on merlin tables containing historical data.
    Don't run this tool unless you're asked to by op5 support staff
    or told to do so by a message during an rpm or yum upgrade.
    """
    print "Adding indexes..."
    print "(if this takes forever, aborting this and running"
    print "\tmon log import --truncate-db"
    print "might be quicker, but could permanently remove some old logs)"
    conn = merlin_db.connect(mconf)
    cursor = conn.cursor()
    log = []
    for table in glob.glob("/opt/monitor/op5/merlin/sql/mysql/*-indexes.sql"):
        queries = file(table)
        try:
            cursor.execute(queries.read())
        except Exception, ex:
            log.append("%s: %s" % (os.path.basename(table), ex[1]))
        queries.close()
    conn.commit()
    conn.close()
    if not log:
        print "Indexes installed successfully"
    else:
        print "\nThere were problems. This is normal if the indexes already",
        print "(partially) exist,\nbut could also indicate a problem."
        print "\nMessages:"
        for msg in log:
            print "  %s" % msg


def module_init(args):
    return args
