#!/usr/bin/env python

import sys
from nacoma.hooks import Change
reportable_types = ['host', 'service']

libexec_dir = "/usr/libexec/merlin/modules"
sys.path.insert(0, libexec_dir)
import merlin_conf as mconf
mconf.parse()
import merlin_db
conn = merlin_db.connect(mconf)
cursor = conn.cursor()

for line in sys.stdin:
    change = Change(line)
    if change.type not in reportable_types:
        continue
    if change.is_renamed():
        if change.type == 'host':
            cursor.execute('INSERT INTO rename_log(from_host_name, from_service_description, to_host_name, to_service_description) VALUES (%s, NULL, %s, NULL)', [change.oldname, change.newname])
        else:
            cursor.execute('INSERT INTO rename_log(from_host_name, from_service_description, to_host_name, to_service_description) VALUES (%s, %s, %s, %s)', change.oldname.split(';') + change.newname.split(';'))

conn.commit()
conn.close()
