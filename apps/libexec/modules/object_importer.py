import sys
import re
from compound_config import parse_conf

class Entry:
	def __repr__(self):
		return '<Entry %s (%s)>' % (self.name, self.id)
	name = None
	id = 1
	instance_id = 0

class Database(object):
	'''A very thin wrapper around the database layer'''
	def __init__(self, db_type='mysql', db_host='localhost', db_name='merlin',
	             db_user='merlin', db_pass='merlin'):
		if db_type == 'mysql':
			import MySQLdb as db
		else:
			print "Invalid database"

		try:
			self.conn = db.connect(host=db_host, user=db_user, passwd=db_pass, db=db_name)
			self.conn.set_character_set('utf8')
			self.db = self.conn.cursor()
		except db.OperationalError, ex:
			print "Couldn't connect to database: %s" % ex.args[1]
			sys.exit(1)

	def sql_exec(self, query, *args):
		'''Run sql query, and print the query if anything breaks.

		There are two modes of operation:
		 1. Sending any number of arguments after the query to be properly
		    escaped and inserted into the function.
		 2. Sending a list or a tuple containing several sequences of arguments
		    after the query. These will be executed in bulk.
		'''
		try:
			if args and (isinstance(args[0], type([])) or isinstance(args[0], type(()))):
				self.db.executemany(query, *args)
			elif args:
				self.db.execute(query, args)
			else:
				self.db.execute(query)
		except Exception, ex:
			raise ex
			if args:
				query = query % args[0]
			raise Exception("%s\nQuery that failed:\n%s" % (ex, query))
	
	def __iter__(self, *args):
		return self.db.__iter__(*args)

	def next(self, *args):
		return self.db.next(*args)

class ObjectIndexer(object):
	'''Used as a temporary data store while tables are being whiped'''
	_indexes = {}
	_rindexes = {}

	def set(self, type, name, id, instance_id = 0):
		if not self._indexes.has_key(type):
			self._indexes[type] = {}
			self._rindexes[type] = {}

		if self._rindexes[type].has_key(id) and self._rindexes[type][id].name != name:
			print "Duplicate $id in ObjectIndexer.set"

		if self._indexes[type].has_key(name) and self._indexes[type][name].id != id:
			print "Attempted to change id of %s object '%s'" % (type, name)

		e = Entry()
		e.name = name
		e.id = id
		e.instance_id = instance_id
		self._rindexes[type][id] = e
		self._indexes[type][name] = e
	
	def get(self, type, name):
		try:
			return self._indexes[type][name]
		except KeyError:
			return False

class AccessCacher(object):
	'''Fills the access_cache table with information about which user is
	allowed access to what objects'''
	def __init__(self, indexer, db):
		self.indexer = indexer
		self.db = db

	def _get_contactgroup_members(self):
		'''Get all members of all user groups'''
		self.db.sql_exec('SELECT contact, contactgroup FROM contact_contactgroup')
		cg_members = {}
		for row in self.db:
			if not cg_members.has_key(row[1]):
				cg_members[row[1]] = set()
			cg_members[row[1]].add(row[0])
		return cg_members
	
	def _cache_cg_object_access_rights(self, cg_members, otype):
		'''Get all mappings from contacts to %(otype) via a contact group'''
		query = 'SELECT %s, contactgroup FROM %s_contactgroup' % (otype, otype)
		self.db.sql_exec(query)
		ret = {}
		for row in self.db:
			if not cg_members.has_key(row[1]):
				print 'Un-cached contactgroup %s assigned to %s %s' % (row[1], otype, row[0])
				continue
			if not ret.has_key(row[0]):
				ret[row[0]] = cg_members[row[1]]
			else:
				ret[row[0]] = ret[row[0]].union(cg_members[row[1]])
		return ret
	
	def _cache_contact_object_access_rights(self, otype):
		'''Get all mappings directly between contacts and %(otype)'''
		query = 'SELECT %s, contact FROM %s_contact' % (otype, otype)
		self.db.sql_exec(query)
		ret = {}
		for row in self.db:
			if not ret.has_key(row[0]):
				ret[row[0]] = set()
			ret[row[0]].add(row[1])
		return ret

	def _write_access_cache(self, obj_list, otype):
		query = 'INSERT INTO contact_access(contact, '+otype+') VALUES (%s, %s)'
		ca_map = []
		for (oid, ary) in obj_list.items():
			for cid in ary:
				ca_map.append((cid, oid))
		self.db.sql_exec(query, ca_map)

	def cache_access_rights(self):
		'''Sets up the contact_access cache table'''
		def merge_dicts(dict1, dict2):
			for key in dict2:
				if not dict1.has_key(key):
					dict1[key] = dict2[key]
				else:
					dict1[key] = dict1[key].union(dict2[key])
			return dict1

		self.db.sql_exec('TRUNCATE contact_access')
		cg_members = self._get_contactgroup_members()
		host = self._cache_cg_object_access_rights(cg_members, 'host')
		service = self._cache_cg_object_access_rights(cg_members, 'service')
		host = merge_dicts(host, self._cache_contact_object_access_rights('host'))
		service = merge_dicts(service, self._cache_contact_object_access_rights('service'))
		self._write_access_cache(host, 'host')
		self._write_access_cache(service, 'service')

class ObjectImporter(object):
	base_oid = {}
	imported = {}
	tables_truncated = False
	importing_status = False
	allowed_vars = {}
	tp_vars = {}
	indexer = ObjectIndexer()

	tables_to_truncate = [
		'command',
		'contact',
		'contact_contactgroup',
		'contactgroup',
		'host_contact',
		'host_contactgroup',
		'host_hostgroup',
		'host_parents',
		'hostdependency',
		'hostescalation',
		'hostescalation_contact',
		'hostescalation_contactgroup',
		'hostgroup',
		'service_contact',
		'service_contactgroup',
		'service_servicegroup',
		'servicedependency',
		'serviceescalation',
		'serviceescalation_contact',
		'serviceescalation_contactgroup',
		'servicegroup',
		'timeperiod',
		'timeperiod_exclude',
		'custom_vars',
		'scheduled_downtime',
		'comment',
	]

	obj_rel = {
		'host': {
			'parents': 'host',
			'contacts': 'contact',
			'contact_groups': 'contactgroup',
		},
		'contact': {
			'host_notification_period': 'timeperiod',
			'service_notification_period': 'timeperiod',
		},
		'hostgroup': {
			'members': 'host',
		},
		'service': {
			'contacts': 'contact',
			'contact_groups': 'contactgroup',
		},
		'contactgroup': {
			'members': 'contact',
		},
		'serviceescalation': {
			'contacts': 'contact',
			'contact_groups': 'contactgroup',
		},
		'servicegroup': {
			'members': 'service',
		},
		'hostdependency': {
			'host_name': 'host',
			'dependent_host_name': 'host',
		},
		'hostescalation': {
			'host_name': 'host',
			'contact_groups': 'contactgroup',
			'contacts': 'contact',
		},
		'timeperiod': {
			'exclude': 'timeperiod',
		}
	}

	convert = {'host':
		{
			'check_execution_time': 'execution_time',
			'plugin_output': 'output',
			'long_plugin_output': 'long_output',
			'enable_notifications': 'notifications_enabled',
			'check_latency': 'latency',
			'performance_data': 'perf_data',
			'normal_check_interval': 'check_interval',
			'retry_check_interval': 'retry_interval',
			'state_history': False,
			'modified_host_attributes': False,
			'modified_service_attributes': False,
			'modified_attributes': False,
		}
	}
	convert['service'] = convert['host']
	convert['contact'] = convert['host']

	conv_type = {
		'program': 'program_status',
		'programstatus': 'program_status',
		'hoststatus': 'host',
		'servicestatus': 'service',
		'contactstatus': 'contact',
		'hostcomment': 'comment',
		'servicecomment': 'comment',
		'hostdowntime': 'scheduled_downtime',
		'servicedowntime': 'scheduled_downtime'
	}


	def __init__(self, db_type='mysql', db_host='localhost', db_name='merlin',
	             db_user='merlin', db_pass='merlin'):
		self.db = Database(db_type, db_host, db_name, db_user, db_pass)

	def disable_indexes(self):
		for table in self.tables_to_truncate:
			self.db.sql_exec('ALTER TABLE '+table+' DISABLE KEYS')
		self.db.conn.commit()
	
	def enable_indexes(self):
		for table in self.tables_to_truncate:
			self.db.sql_exec('ALTER TABLE '+table+' ENABLE KEYS')

	def prepare_import(self):
		'''All tables are truncated and reimported. This method stores hosts'
		and services' old IDs, so they get them back afterwards'''
		self._preload_object_index('host',
			'SELECT instance_id, id, host_name FROM host')
		self._preload_object_index('service',
			"SELECT instance_id, id, CONCAT(host_name, ';', service_description) FROM service")

		self.db.sql_exec('DESCRIBE timeperiod')
		for col in self.db:
			self.tp_vars[col[0]] = col[0]

	def import_objects_from_cache(self, cache, verbose=False):
		'''Silly wrapper that only exists to print prettier backtraces to the
		user'''
		if verbose:
			return self._import_objects_from_cache(cache)
		else:
			try:
				return self._import_objects_from_cache(cache)
			except Exception, ex:
				if type(ex) == KeyError:
					# message is only the faulting key - write something prettier
					print "Error when importing file: either the file is broken, or this indicates a bug"
				else:
					print "Error when importing file: %s" % ex

				# We're probably using myisam, in which case this is a noop,
				# but let's try anyway...
				try:
					self.db.conn.rollback()
				except:
					pass

	def _import_objects_from_cache(self, cache):
		'''Given a file name, insert all objects in it into the database'''
		last_obj_type = ''
		obj_array = {}

		if not self.tables_truncated:
			for table in self.tables_to_truncate:
				self.db.sql_exec('TRUNCATE ' + table)
			self.tables_truncated = True

		service_slaves = {'serviceescalation': 1, 'servicedependency': 1}

		try:
			root = parse_conf(cache)
		except IOError:
			print "Error: Couldn't open file '%s'." % cache
			return
		if not root.objects:
			print "Couldn't parse file."
			return
		if root.objects[0].name in ('info', 'program'):
			self.importing_status = True

		for obj in root.objects:
			obj_type = obj.name
			if obj_type.startswith('define '):
				obj_type = obj_type[7:]
			if self.conv_type.has_key(obj_type):
				obj_type = self.conv_type[obj_type]

			if obj_type != last_obj_type:
				obj_array = self._done_parsing_obj_type_objects(last_obj_type, obj_array)
				last_obj_type = obj_type

			obj_data = self._mangle_data(obj_type, obj.params)

			if not obj_data:
				continue

			obj_name = self._get_obj_name(obj_type, obj_data)
			old_obj = self.indexer.get(obj_type, obj_name)

			if not old_obj:
				old_obj = Entry()
				old_obj.name = obj_name

				if not self.base_oid.has_key(obj_type):
					self.base_oid[obj_type] = 1

				old_obj.id = self.base_oid[obj_type]
				self.base_oid[obj_type] += 1
				obj_data['is a fresh one'] = True
				if obj_name:
					self.indexer.set(obj_type, obj_name, old_obj.id)

			if old_obj.instance_id != 0:
				obj_data['instance_id'] = old_obj.instance_id
			obj_key = old_obj.id

			if obj_type in ('host', 'program_status'):
				pass
			elif obj_type == 'timeperiod':
				pass
			elif obj_type not in ('hostgroup', 'servicegroup', 'contactgroup'):
				if service_slaves.has_key(obj_type):
					obj_data = self._post_mangle_service_slave(obj_type,
					                                           obj_data)

			if obj_data:
				if not obj_array.has_key(obj_type):
					obj_array[obj_type] = {}
				obj_array[obj_type][obj_key] = obj_data

		obj_array = self._done_parsing_obj_type_objects(obj_type, obj_array)

	def _done_parsing_obj_type_objects(self, obj_type, obj_array):
		'''All objects type are grouped by nagios. This is called when one of
		the types is done to do the actual insertion of all objects'''
		if not (obj_type and obj_array):
			return obj_array

		if obj_type in ('host', 'timeperiod'):
			obj_array[obj_type] = self._post_mangle_self_ref(obj_type,
				obj_array[obj_type])

		# make sure {hosts,services,contacts} are parsed and cached, so
		# {host,service,contact}group membership can be properly calculated
		if obj_type in ('host', 'service', 'contact'):
			group = obj_type + 'group'
			if obj_array.has_key(group):
				obj_array[group] = self._post_mangle_groups(group,
					obj_array[group])
				self._glue_objects(obj_array[group], group)
		elif obj_type.endswith('group') and not obj_array.has_key(obj_type[:-5]):
			return obj_array

		self._glue_objects(obj_array[obj_type], obj_type)
		return obj_array

	def _glue_objects(self, obj_list, obj_type):
		'''Insert all objects of a type into the database'''
		if not obj_list:
			return True
		for (obj_key, obj) in obj_list.items():
			self._glue_object(obj_key, obj_type, obj)

	def _glue_object(self, obj_key, obj_type, obj):
		'''Insert an object into the database'''
		if self.conv_type.has_key(obj_type):
			obj_type = self.conv_type[obj_type]

		if self.importing_status:
			if obj_type == 'host' or obj_type == 'service':
				if obj.get('current_state') == '0' and obj.get('has_been_checked') == '0':
					obj['current_state'] = '6'

		if not obj_type:
			return

		fresh = obj.has_key('is a fresh one')

		if fresh:
			del obj['is a fresh one']

		if obj_type == 'host' or obj_type == 'service':
			obj_name = self._get_obj_name(obj_type, obj)

			if self.imported.has_key(obj_type) and \
			   self.imported[obj_type].get(obj_key, obj_name) != obj_name:
				print 'overwriting %s id %s in self.imported' % \
					(obj_type, obj_key)
				print obj
				sys.exit(0)

			if not self.imported.has_key(obj_type):
				self.imported[obj_type] = {}
			self.imported[obj_type][obj_key] = self._get_obj_name(obj_type, obj)

		if self.obj_rel.has_key(obj_type):
			spec = self.obj_rel[obj_type]

		if obj_type == 'program_status':
			obj['instance_id'] = 0
			obj['instance_name'] = 'Local Nagios/Merlin instance'
			obj['is_running'] = 1
		else:
			if obj_type == 'comment':
				# According to nagios/comments.h:
				# #define HOST_COMMENT 1
				# #define SERVICE_COMMENT 2
				# We can't use "empty()" here, or a service_description
				# of '0' will cause it to be considered a host comment.
				if obj.get('service_description'):
					obj['comment_type'] = 2
				else:
					if obj.has_key('service_description'):
						del obj['service_description']
					obj['comment_type'] = 1
			elif obj_type == 'scheduled_downtime':
				# According to nagios/common.h:
				# #define SERVICE_DOWNTIME 1
				# #define HOST_DOWNTIME 2
				# #define ANY_DOWNTIME 3
				# that last one is a bit weird...
				if obj.get('service_description'):
					obj['downtime_type'] = 1
				else:
					if obj.has_key('service_description'):
						del obj['service_description']
					obj['downtime_type'] = 2
			obj['id'] = obj_key

		if obj.has_key('__custom'):
			custom = obj['__custom']
			del obj['__custom']
		else:
			custom = False

		for (k, v) in obj.items():
			if type(v) in (dict, list, set):
				del obj[k]
				junction = self._get_junction_table_name(obj_type, k)

				for junc_part in v:
					other_obj_type = spec[k]

					if other_obj_type == obj_type:
						other_obj_type = k

					query = 'INSERT INTO %s (%s, %s)' % (junction, obj_type, other_obj_type)
					self.db.sql_exec(query + ' VALUES (%s, %s)', obj_key, junc_part)
			else:
				try:
					float(v)
					obj[k] = "'%s'" % v
				except:
					obj[k] = "'%s'" % self.db.conn.escape_string(v.encode('utf-8')).decode('utf-8')

		if (not fresh and (obj_type in ('host', 'service'))) or \
		   (obj_type == 'contact' and self.importing_status):
			query = 'UPDATE %s SET ' % (obj_type,)
			oid = obj['id']
			del obj['id']
			params = []
			for (k, v) in obj.items():
				params.append('%s = %s' % (k, v))

			query += ', '.join(params) + (' WHERE id = %s' % oid)
		else:
			target_vars = ','.join(obj.keys())
			target_values = ','.join(obj.values())
			query = 'REPLACE INTO %s(%s) VALUES (%s)' % (obj_type, target_vars, target_values)

		self.db.sql_exec(query)

		self._glue_custom_vars(obj_type, obj_key, custom)

		return True
	
	def _glue_custom_vars(self, obj_type, obj_id, custom = None):
		'''Replace all stored custom vars in the database for an object type'''
		ret = True

		if custom == None:
			return True

		esc_obj_type = self.db.conn.escape_string(obj_type)
		esc_obj_id = self.db.conn.escape_string(str(obj_id))
		purge_query = "DELETE FROM custom_vars WHERE obj_type = '%s' AND obj_id = '%s'" % (esc_obj_type, esc_obj_id)

		result = self.db.sql_exec(purge_query)
		if not custom:
			return result

		query = "INSERT INTO custom_vars VALUES(%s, %s, %s, %s)"
		cv_map = []

		for (k, v) in custom.items():
			cv_map.append((esc_obj_type, esc_obj_id, k, v))
		self.db.sql_exec(query, cv_map)

		return ret

	def _preload_object_index(self, obj_type, query):
		'''Given a query, store all objects in the cache index'''
		self.db.sql_exec(query)
		index_max = 1
		for row in self.db:
			self.indexer.set(obj_type, row[2], row[1], row[0])
			if row[1] >= index_max:
				index_max = row[1] + 1

		self.base_oid[obj_type] = index_max
	
	def _get_obj_name(self, obj_type, obj):
		'''Get a proper identifier for an object'''
		if obj.has_key(obj_type + '_name'):
			return obj[obj_type + '_name']

		if obj_type == 'service':
			return '%s;%s' % (obj['host_name'], obj['service_description'])

		return False
	
	def _mangle_data(self, obj_type, data):
		'''Converts a CompoundConfig object into a dict, and cleans up a few
		keys in the process'''
		if self.obj_rel.has_key(obj_type):
			relation = self.obj_rel[obj_type]
		else:
			relation = {}
		out = {}
		for (key, val) in data:
			k = self._mangle_var_name(obj_type, key)
			if not (k and self._is_allowed_var(obj_type, k)):
				continue

			if val == '':
				continue

			if k in ('members', 'parents', 'exclude'):
				out[k] = val
			elif k in ('contacts', 'contact_groups'):
				ary = re.split(r'[\t ]*,[\t ]*', val)
				v_ary = set()
				for v in ary:
					v_ary.add(self.indexer.get(relation[k], v).id)
				out[k] = v_ary
			else:
				if k[0] == '_' or \
				   (obj_type == 'timeperiod' and not self.tp_vars.has_key(k)):
					out['__custom'] = {k: val}
				elif relation.has_key(k):
					if relation[k] == 'command' and val.find('!') >= 0:
						ary = val.split('!')
						val = ary[0]
						out[k + '_args'] = ary[1]
					val = self.indexer.get(relation[k], val)
					if val:
						val = val.id
				out[k] = val
		return out

	def _mangle_var_name(self, obj_type, k):
		'''Some properties needs to be renamed, others are not allowed - handle
		here'''
		if not k:
			print 'Found empty $k with obj_type ' + obj_type
			sys.exit(1)

		if self.convert.has_key(obj_type) and self.convert[obj_type].has_key(k):
			return self.convert[obj_type][k]

		if obj_type == 'host':
			if k in ('vrml_image', '3d_coords', '2d_coords'):
				return False
		elif obj_type == 'program_status':
			if k.startswith('enable_'):
				return k[len('enable_'):] + '_enabled'

			if k in ('normal_check_interval', 'next_comment_id', 'next_downtime_id', 'next_event_id', 'next_problem_id', 'next_notification_id'):
				return False
		elif obj_type == 'comment':
			if k == 'author':
				return 'author_name'

		return k
	
	def _post_mangle_groups(self, group, obj_list):
		'''Split member properties into lists'''
		if not obj_list:
			return obj_list

		ref_type = group[:0-len('group')]

		for obj_key in obj_list:
			if not obj_list[obj_key].get('members'):
				continue

			ary = re.split(r'[\t ]*,[\t ]*', obj_list[obj_key]['members'])
			v_ary = []
			if group == 'servicegroup':
				while ary:
					srv = ary.pop()
					host = ary.pop()
					v_ary.append(self.indexer.get('service', '%s;%s' % (host, srv)).id)
			else:
				for mname in ary:
					index = self.indexer.get(ref_type, mname)
					if index:
						index = index.id
					v_ary.append(index)
			obj_list[obj_key]['members'] = v_ary
		return obj_list

	def _post_mangle_self_ref(self, obj_type, obj_list):
		'''Split parents/exclude properties into sets'''
		if not obj_list:
			return obj_list
		if obj_type == 'host':
			k = 'parents'
		else:
			k = 'exclude'

		for oid in obj_list:
			if not obj_list[oid].has_key(k):
				continue
			ary = re.split(r'[\t ]*,[\t ]*', obj_list[oid][k])
			obj_list[oid][k] = []
			for v in ary:
				index = self.indexer.get(obj_type, v)
				if index:
					index = index.id
				obj_list[oid][k].append(index)
		return obj_list

	def _post_mangle_service_slave(self, obj_type, obj):
		'''Mangle an event that is related to a service'''
		srv = '%s;%s' % (obj['host_name'], obj['service_description'])
		obj['service'] = self.indexer.get('service', srv).id
		del obj['host_name']
		del obj['service_description']

		if obj_type == 'servicedependency':
			srv = '%s;%s' % (obj['dependent_host_name'], obj['dependent_service_description'])
			obj['dependent_service'] = self.indexer.get('service', srv).id
			del obj['dependent_host_name']
			del obj['dependent_service_description']
		return obj

	def _is_allowed_var(self, obj_type, k):
		'''Find legal properties for an object - generally it's database
		fields'''
		if not self.importing_status:
			return True

		if obj_type == 'info':
			return False

		if not self.allowed_vars.has_key(obj_type):
			self.db.sql_exec('describe ' + obj_type)

			if not self.allowed_vars.has_key(obj_type):
				self.allowed_vars[obj_type] = set()
			for row in self.db:
				self.allowed_vars[obj_type].add(row[0])

		return k in self.allowed_vars[obj_type]

	def _get_junction_table_name(self, obj_type, v_name):
		ref_obj_type = self.obj_rel[obj_type][v_name]
		ret = '%s_%s' % (obj_type, ref_obj_type)

		if v_name == 'members':
			ret = '%s_%s' % (ref_obj_type, obj_type)
		elif ref_obj_type == obj_type:
			ret = '%s_%s' % (obj_type, v_name)

		return ret

	def finalize_import(self):
		self._purge_old_objects()
		cacher = AccessCacher(self.indexer, self.db)
		cacher.cache_access_rights()
		self.db.conn.commit()
		self.db.conn.close()

	def _purge_old_objects(self):
		for (obj_type, ids) in self.imported.items():
			query = "DELETE FROM %s WHERE id NOT IN ('%s')" % \
				(obj_type, "','".join([str(x) for x in ids.keys()]))
			self.db.sql_exec(query)
