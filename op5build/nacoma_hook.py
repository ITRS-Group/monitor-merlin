#!/usr/bin/env python

import sys
from nacoma.hooks import Change
reportable_types = ['host', 'service']

libexec_dir = "/usr/libexec/merlin/mon/modules"
sys.path.insert(0, libexec_dir)
import merlin_conf as mconf
mconf.parse()
import merlin_db
conn = merlin_db.connect(mconf)
cursor = conn.cursor()

# this terrible kludge is required because merlin_db gives us no way to access the database module
mysqlparamstyle = 'format'
oracleparamstyle = 'named'
if 'Oracle' in str(type(conn)):
    paramstyle = oracleparamstyle
else:
    paramstyle = mysqlparamstyle

for line in sys.stdin:
    change = Change(line)
    if change.type not in reportable_types:
        continue
    if change.is_renamed():
        if change.type == 'host':
            arg = (change.oldname, change.newname)
            if paramstyle == 'named':
                arg = dict(zip(('oldhost', 'newhost'), arg))
                query = 'INSERT INTO rename_log(from_host_name, from_service_description, to_host_name, to_service_description) VALUES (:oldhost, NULL, :newhost, NULL)'
            elif paramstyle == 'format':
                query = 'INSERT INTO rename_log(from_host_name, from_service_description, to_host_name, to_service_description) VALUES (%s, NULL, %s, NULL)'
            cursor.execute(query, arg)
        else:
            arg = change.oldname.split(';') + change.newname.split(';')
            if paramstyle == 'named':
                arg = dict(zip(('oldhost', 'oldservice', 'newhost', 'newservice'), arg))
                query = 'INSERT INTO rename_log(from_host_name, from_service_description, to_host_name, to_service_description) VALUES (:oldhost, :oldservice, :newhost, :newservice)'
            elif paramstyle == 'format':
                query = 'INSERT INTO rename_log(from_host_name, from_service_description, to_host_name, to_service_description) VALUES (%s, %s, %s, %s)'
            cursor.execute(query, arg)
conn.commit()
conn.close()
