<?php


class object_indexer
{
	private $idx = array();
	private $ridx = array();
	public $db = FALSE;
	public $debug = FALSE;
	public function set($type, $name, $id)
	{
		if (!is_numeric($id)) {
			die("\$id not numeric in object_indexer::set\n");
		}
		$id = intval($id);

		if (!isset($this->idx[$type])) {
			$this->idx[$type] = array();
			$this->ridx[$type] = array();
		}

		if (isset($this->ridx[$type][$id]) && $this->ridx[$type][$id] !== $name)
			die("Duplicate \$id in object_indexer::set\n");

		if (isset($this->idx[$type][$name]) && $this->idx[$type][$name] !== $id)
			die("Attempted to change id of $type object '$name'\n");

		$this->ridx[$type][$id] = $name;
		$this->idx[$type][$name] = $id;
		return true;
	}

	public function get($type, $name, $must_exist = false)
	{
		if (!isset($this->idx[$type]) || !isset($this->idx[$type][$name])) {
			if ($must_exist)
				die("Failed to locate a $type object named '$name'\n");

			return false;
		}

		return $this->idx[$type][$name];
	}

	public function get_objects_for_type($type)
	{
		if (!isset($this->ridx[$type])) {
			return false;
		}
		return $this->ridx[$type];
	}
}

class nagios_object_importer
{
	public $instance_id = 0;
	public $db_quote_special = false;

	protected $total_queries = 0;
	public $db_type = 'mysql';
	public $db_host = 'localhost';
	public $db_user = '@@DBUSER@@';
	public $db_pass = '@@DBPASS@@';
	public $db_database = '@@DBNAME@@';
	public $db_port = 3306;
	protected $db = false;

	public $errors = 0;

	public $debug = false;

	# internal object indexing cache
	private $idx; # object_indexer object
	private $base_oid = array();
	private $imported = array();

	# denotes if we're importing status.sav or objects.cache
	private $importing_status = false;

	private $tables_to_truncate = array
		('command' => 1,
		 'contact' => 1,
		 'contact_contactgroup' => 1,
		 'contactgroup' => 1,
		 'host_contact' => 1,
		 'host_contactgroup' => 1,
		 'host_hostgroup' => 1,
		 'host_parents' => 1,
		 'hostdependency' => 1,
		 'hostescalation' => 1,
		 'hostescalation_contact' => 1,
		 'hostescalation_contactgroup' => 1,
		 'hostgroup' => 1,
		 'service_contact' => 1,
		 'service_contactgroup' => 1,
		 'service_servicegroup' => 1,
		 'servicedependency' => 1,
		 'serviceescalation' => 1,
		 'serviceescalation_contact' => 1,
		 'serviceescalation_contactgroup' => 1,
		 'servicegroup' => 1,
		 'timeperiod' => 1,
		 'timeperiod_exclude' => 1,
		 'custom_vars' => 1,
		 'scheduled_downtime' => 1,
		 'comment_tbl' => 1,
		 'contact_access' => 1,
		 );

	# conversion table for variable names
	private $convert = array();
	private $conv_type = array
		('info' => false, 'program' => 'program_status',
		 'programstatus' => 'program_status',
		 'hoststatus' => 'host', 'servicestatus' => 'service',
		 'contactstatus' => 'contact',
		 'hostcomment' => 'comment_tbl', 'servicecomment' => 'comment_tbl',
		 'hostdowntime' => 'scheduled_downtime',
		 'servicedowntime' => 'scheduled_downtime');

	# allowed variables for each object
	private $allowed_vars = array();

	# object relations table used to determine db junction table names etc
	protected $Object_Relation = array();

	private $is_full_import = false;

	# these columns will always be reset to the default value if unset
	private $columns_to_clean = array(
		'host' => array(
			'notes' => 1,
			'notes_url' => 1,
			'action_url' => 1,
			'icon_image' => 1,
			'icon_image_alt' => 1,
			'statusmap_image' => 1
		),
		'service' => array(
			'notes' => 1,
			'notes_url' => 1,
			'action_url' => 1,
			'icon_image' => 1,
			'icon_image_alt' => 1
		)
	);

	public function __construct()
	{
		$this->obj_rel['host'] =
			array('parents' => 'host',
				  'contacts' => 'contact',
				  'contact_groups' => 'contactgroup'
				  );

		$this->obj_rel['contact'] =
			array('host_notification_period' => 'timeperiod',
			      'service_notification_period' => 'timeperiod',
			      );

		$this->obj_rel['hostgroup'] =
			array('members' => 'host');

		$this->obj_rel['service'] =
			array('contacts' => 'contact',
				  'contact_groups' => 'contactgroup',
				  );

		$this->obj_rel['contactgroup'] =
			array('members' => 'contact');

		$this->obj_rel['serviceescalation'] =
			array('contacts' => 'contact',
				  'contact_groups' => 'contactgroup',
				  );

		$this->obj_rel['servicegroup'] =
			array('members' => 'service');

		$this->obj_rel['hostdependency'] =
			array('host_name' => 'host',
				  'dependent_host_name' => 'host',
				  );

		$this->obj_rel['hostescalation'] =
			array('host_name' => 'host',
				  'contact_groups' => 'contactgroup',
				  'contacts' => 'contact',
				  'contact_groups' => 'contactgroup',
				  );

		$this->obj_rel['timeperiod'] =
			array('exclude' => 'timeperiod');
		$this->convert['host'] = array
			('check_execution_time' => 'execution_time',
			 'plugin_output' => 'output',
			 'long_plugin_output' => 'long_output',
			 'enable_notifications' => 'notifications_enabled',
			 'check_latency' => 'latency',
			 'performance_data' => 'perf_data',
			 'normal_check_interval' => 'check_interval',
			 'retry_check_interval' => 'retry_interval',
			 'state_history' => false,
			 'modified_host_attributes' => false,
			 'modified_service_attributes' => false,
			 'modified_attributes' => false,
			 );
		$this->convert['service'] = $this->convert['host'];
		$this->convert['contact'] = $this->convert['host'];

		$this->tables_to_modify = $this->tables_to_truncate;

		$this->idx = new object_indexer;
	}

	private function isMySQL()
	{
		return $this->db && ('mysql' === $this->db->driverName);
	}
	private function isOCI()
	{
		return $this->db && ('oci' === $this->db->driverName);
	}

	private function concat($list) {
		if( $this->isMySQL() ) {
		    return 'CONCAT('.implode(', ',$list).')';
		}
		else {
		    return implode(' || ',$list);
		}
	}

	public function disable_indexes()
	{
		if ($this->isMySQL()) {
			foreach ($this->tables_to_modify as $t => $v) {
				$this->sql_exec_query('ALTER TABLE ' . $t . ' DISABLE KEYS');
			}
		}
	}

	public function enable_indexes()
	{
		if ($this->isMySQL()) {
			foreach ($this->tables_to_modify as $table => $v) {
				$this->sql_exec_query('ALTER TABLE ' . $table . ' ENABLE KEYS');
			}
		}
	}

	private function get_junction_table_name($obj_type, $v_name)
	{
		$ref_obj_type = $this->obj_rel[$obj_type][$v_name];
		$ret = $obj_type . '_' . $ref_obj_type;

		if($v_name === 'members')
			$ret = $ref_obj_type . '_' . $obj_type;
		elseif($ref_obj_type === $obj_type)
			$ret = $obj_type . '_' . $v_name;

		return $ret;
	}

	private function post_mangle_groups($group, &$obj_list)
	{
		if (empty($obj_list))
			return;

		$ref_type = str_replace('group', '', $group);

		foreach ($obj_list as $obj_key => &$obj) {
			# empty groups are ok. Just insert them early
			# and ignore them
			if (empty($obj['members'])) {
				$this->glue_object($obj_key, $group, $obj);
				continue;
			}

			$ary = preg_split("/[\t ]*,[\t ]*/", $obj['members']);
			$v_ary = array(); # reset between each loop
			if ($group === 'servicegroup') {
				while ($srv = array_pop($ary)) {
					$host = array_pop($ary);
					$v_ary[] = $this->idx->get('service', "$host;$srv");
				}
			}
			else foreach ($ary as $mname) {
				$v_ary[] = $this->idx->get($ref_type, $mname);
			}
			$obj['members'] = $v_ary;
			$this->glue_object($obj_key, $group, $obj);
		}
	}

	private function post_mangle_self_ref($obj_type, &$obj_list)
	{
		if (empty($obj_list))
			return false;

		if ($obj_type === 'host')
			$k = 'parents';
		else
			$k = 'exclude';

		foreach ($obj_list as $id => &$obj) {
			if (!isset($obj[$k]))
				continue;
			$ary = preg_split("/[\t ]*,[\t ]*/", $obj[$k]);
			$obj[$k] = array();
			foreach ($ary as $v)
				$obj[$k][] = $this->idx->get($obj_type, $v);
		}
	}

	private function post_mangle_service_slave($obj_type, &$obj)
	{
		$srv = $obj['host_name'] . ';' . $obj['service_description'];
		$obj['service'] = $this->idx->get('service', $srv);
		unset($obj['host_name']);
		unset($obj['service_description']);

		if ($obj_type === 'servicedependency') {
			$srv = $obj['dependent_host_name'] . ';' . $obj['dependent_service_description'];
			$obj['dependent_service'] = $this->idx->get('service', $srv);
			unset($obj['dependent_host_name']);
			unset($obj['dependent_service_description']);
		}
	}

	function sql_commit()
	{
		if ($this->is_oracle()) {
			$this->db->commit();
		} else {
			$this->db->query("COMMIT");
		}
	}

	/**
	 * Invoked when we encounter a new type of object in objects.cache
	 * Since objects are grouped by type in that file, this means we're
	 * done entirely parsing the type of objects passed in $obj_type
	 */
	private function done_parsing_obj_type_objects($obj_type, &$obj_array)
	{
		if ($obj_type === false || empty($obj_array))
			return true;

		$this->sql_commit();

		switch ($obj_type) {
		 case 'host': case 'timeperiod':
			$this->post_mangle_self_ref($obj_type, $obj_array[$obj_type]);
			$this->glue_objects($obj_array[$obj_type], $obj_type);
			unset($obj_array[$obj_type]);
			# fallthrough, although no-op for timeperiods
		 case 'service': case 'contact':
			$group = $obj_type . 'group';
			if (isset($obj_array[$group])) {
				$this->post_mangle_groups($group, $obj_array[$group]);
				unset($obj_array[$group]);
			}
			break;
		}

		return true;
	}


	public function prepare_import()
	{
		# preload object indexes only once per import run
		$this->preload_object_index('host', 'SELECT id, host_name FROM host');
		$this->preload_object_index('service', "SELECT id, " .
						       $this->concat(array('host_name', "';'", 'service_description')) .
						       " FROM service");
		# These two shouldn't be needed in full imports, but we don't know if
		# that's what we have yet. Shouldn't hurt too much.
		$this->preload_object_index('contact', 'SELECT id, contact_name FROM contact');
		$this->preload_object_index('timeperiod', 'SELECT id, timeperiod_name FROM timeperiod');

		if (!$this->is_oracle()) {
			# this has no effect on MyISAM tables but works
			# wonders for performance on InnoDB tables.
			# postgres might not support it though.
			$this->sql_exec_query("SET autocommit = 0");
		}
	}

	public function finalize_import()
	{
		$this->purge_old_objects();
		$this->cache_access_rights();
		$this->enable_indexes();
		$this->sql_commit();
		echo "Import finalized. Total queries: $this->total_queries\n";
	}

	private function get_contactgroup_members()
	{
		$result = $this->sql_exec_query
			("SELECT contact, contactgroup from contact_contactgroup");

		$cg_members = array();
		while ($row = $result->fetch(PDO::FETCH_NUM)) {
			$cg_members[$row[1]][$row[0]] = $row[0];
		}

		return $cg_members;
	}

	private function cache_cg_object_access_rights($cg_members, $otype)
	{
		$query = "SELECT $otype, contactgroup FROM {$otype}_contactgroup";
		$result = $this->sql_exec_query($query);
		$ret = array();
		while ($row = $result->fetch(PDO::FETCH_NUM)) {
			if (empty($cg_members[$row[1]])) {
				# empty contactgroup most likely
				continue;
			}
			if (!empty($ret[$row[0]])) {
				foreach ($cg_members[$row[1]] as $cid) {
					$ret[$row[0]][$cid] = $cid;
				}
			} else {
				$ret[$row[0]] = $cg_members[$row[1]];
			}
		}
		return $ret;
	}

	private function cache_contact_object_access_rights($otype, &$ret)
	{
		$query = "SELECT $otype, contact FROM {$otype}_contact";
		$result = $this->sql_exec_query($query);
		while ($row = $result->fetch(PDO::FETCH_NUM)) {
			$ret[$row[0]][$row[1]] = $row[1];
		}
		return $ret;
	}

	private function write_access_cache($obj_list, $otype)
	{
		foreach ($obj_list as $id => $ary) {
			foreach ($ary as $cid) {
				$query = "INSERT INTO contact_access(contact, $otype) " .
					"VALUES($cid, $id)";
				$this->sql_exec_query($query);
			}
		}
	}

	public function cache_access_rights()
	{
		echo "Caching contact access rights\n";
		$cg_members = $this->get_contactgroup_members();
		$ary['host'] = $this->cache_cg_object_access_rights($cg_members, 'host');
		$ary['service'] = $this->cache_cg_object_access_rights($cg_members, 'service');
		$this->cache_contact_object_access_rights('host', $ary['host']);
		$this->cache_contact_object_access_rights('service', $ary['service']);
		$this->write_access_cache($ary['host'], 'host');
		$this->write_access_cache($ary['service'], 'service');
	}

	private function preload_object_index($obj_type, $query)
	{
		$result = $this->sql_exec_query($query);
		$idx_max = 1;
		while ($row = $result->fetch(PDO::FETCH_NUM)) {
			$this->idx->set($obj_type, $row[1], $row[0]);
			if ($row[0] >= $idx_max)
				$idx_max = $row[0] + 1;
		}
		$this->base_oid[$obj_type] = $idx_max;
	}

	private function purge_old_objects()
	{
		foreach ($this->imported as $obj_type => $ids) {
			# oracle has a limit for 1000 entries in an IN
			# list, so we have to work around it by chaining
			# them 1000 at a time. The overhead should be
			# negligible for most cases.
			$query = "DELETE FROM $obj_type WHERE ";
			$ak_ids = array_keys($ids);
			$offset = 0;
			$num_ak_ids = count($ak_ids);
			for ($offset = 0; $offset + 1000 < $num_ak_ids; $offset += 1000) {
				$query .= 'id NOT IN(' . join(',', array_slice($ak_ids, $offset, 1000)) . ')';
				$query .= ' AND ';
			}
			$query .= 'id NOT IN(' .
				join(',', array_slice($ak_ids, $offset, $num_ak_ids - $offset)) .
				')';

			$result = $this->sql_exec_query($query);
			$this->sql_commit();
		}
	}

	/**
		Similar to MySQL's "DESCRIBE $table". Returns an ARRAY of ARRAYS. Each sub-array
		contains two entries (0=>FIELDNAME, 'Field'=>FIELDNAME). i.e. it simply holds
		the field names for the table at the 0th index and in a field named 'Field' (for
		MySQL compatibility).

		Works with the MySQL and OCI back-ends. Requires additional code for other
		back-ends.

		BUGS: this function TO-LOWERs the result fields
		because Oracle returns only upper-case column names
		(which breaks this code's field mappings). i.e. all returned fields
		will be lower-case, which will break downstream code if the fields are really
		mixed-case.
	*/
	private function describe($table) {
		$query = false;
		if( $this->isMySQL() ) {
		    $query = "DESCRIBE $table";
		}
		else if( $this->isOCI() ) {
		     $query = "SELECT COLUMN_NAME FROM USER_TAB_COLUMNS WHERE TABLE_NAME ='".strtoupper($table)."'";
		}
		if( false === $query) {
		     throw new Exception("Don't know how to DESCRIBE tables for db driver [".$this->db->driverName."]!");
		}
		$res = $this->sql_exec_query($query);
		$rc = array();
		while( ($row = $res->fetch(PDO::FETCH_NUM)) ) {
		       $low = strtolower($row[0]);
		       $rc[] = array(0=>$low, 'Field'=>$low);
		}
		return $rc;
	}

	private function is_allowed_var($obj_type, $k)
	{
		if (!$this->importing_status)
			return true;

		if ($obj_type === 'info')
			return false;

		if (!isset($this->allowed_vars[$obj_type])) {
			$list = $this->describe("$obj_type");
			if (empty($list))
				return false;

			foreach($list as $lkey => $row) {
				$this->allowed_vars[$obj_type][$row[0]] = $row[0];
			}
		}

		return isset($this->allowed_vars[$obj_type][$k]);
	}

	private function mangle_var_name($obj_type, $k)
	{
		if (empty($k)) {
			echo("Found empty \$k with obj_type $obj_type\n");
			echo var_dump($k);
			exit(1);
		}

		if (isset($this->convert[$obj_type][$k]))
			return $this->convert[$obj_type][$k];

		if ($obj_type === 'host') {
			if ($k === 'vrml_image' || $k === '3d_coords' ||
				$k === '2d_coords')
			{
				return false;
			}
		} elseif ($obj_type === 'program_status') {
			if (substr($k, 0, strlen('enable_')) === 'enable_') {
				return substr($k, strlen('enable_')) . '_enabled';
			}
			switch ($k) {
			 case 'normal_check_interval': case 'next_comment_id':
			 case 'next_downtime_id': case 'next_event_id':
			 case 'next_problem_id': case 'next_notification_id':
				return false;
				break;
			}
		} elseif ($obj_type === 'comment_tbl') {
			if ($k === 'author')
				return 'author_name';
		}
		return $k;
	}

	function import_objects_from_cache($object_cache = false, $is_cache = true)
	{
		$last_obj_type = false;
		$obj_type = false;

		if (!$object_cache)
			$object_cache = '/opt/monitor/var/objects.cache';

		$this->importing_status = !$is_cache;

		if ($is_cache) {
			$this->is_full_import = true;
			# In the case of a partial import, we don't truncate before we
			# need to, to hide that we're doing it. However, with a full
			# import, it's more important to remove all incorrect data, so
			# make sure that everything's truncated in that case.
			foreach($this->tables_to_truncate as $table => $v)
				$this->sql_exec_query("TRUNCATE TABLE $table");
			$this->table_to_truncate = array();
		}

		# service slave objects are handled separately
		$service_slaves =
			array('serviceescalation' => 1,
				  'servicedependency' => 1);

		if (!file_exists($object_cache))
			return false;
		else
			$fh = fopen($object_cache, "r");

		if (!$fh)
			return false;

		# fetch 'real' timeperiod variables so we can exclude all
		# others as custom variables
		$result = $this->describe('timeperiod');
		$tp_vars = array();
		foreach($result as $k => $ary) {
			$tp_vars[$ary['Field']] = $ary['Field'];
		}
		unset($result);

		while (!feof($fh)) {
			$line = trim(fgets($fh));

			if (empty($line) || $line{0} === '#')
				continue;

			if (!$obj_type) {
				$obj = array();
				$str = explode(' ', $line, 3);
				if ($str[0] === 'define') {
					$obj_type = $str[1];
				} else {
					$obj_type = $str[0];
				}

				if (!empty($this->conv_type[$obj_type])) {
					$obj_type = $this->conv_type[$obj_type];
				}

				# get rid of objects as early as we can
				if ($obj_type !== $last_obj_type) {
					if (isset($this->obj_rel[$obj_type]))
						$relation = $this->obj_rel[$obj_type];
					else
						$relation = false;

					$this->done_parsing_obj_type_objects($last_obj_type, $obj_array);

					$last_obj_type = $obj_type;

					# If we're doing a full import, all tables are truncated, so nothing
					# will happen here. If we're doing a partial import, all junction
					# tables will need to be cleared, as will objects that doesn't
					# exist in objects.cache.
					if ($obj_type == 'comment' || $obj_type == 'scheduled_downtime') {
						if (isset($this->tables_to_truncate[$obj_type])) {
							$this->sql_exec_query("TRUNCATE TABLE $obj_type");
							unset($this->tables_to_truncate[$obj_type]);
						}
						if (isset($this->obj_rel[$obj_type])) {
							foreach ($this->obj_rel[$obj_type] as $k => $v) {
								$junc = $this->get_junction_table_name($obj_type, $k);
								if (isset($this->tables_to_truncate[$junc])) {
									$this->sql_exec_query("TRUNCATE TABLE $junc");
									unset($this->tables_to_truncate[$junc]);
								}
							}
						}
					}
				}
				$obj = array();
				continue;
			}

			// we're inside an object now, so check for closure and tag if so
			$str = preg_split("/[\t=]/", $line, 2);

			// end of object? check type and populate index table
			if ($str[0] === '}') {
				$obj_name = $this->obj_name($obj_type, $obj);
				$obj_key = $this->idx->get($obj_type, $obj_name);
				if (!$obj_key) {
					$obj_key = 1;
					if (!isset($this->base_oid[$obj_type])) {
						$this->base_oid[$obj_type] = 1;
					}
					$obj_key = $this->base_oid[$obj_type]++;
					$obj['is a fresh one'] = true;
					if ($obj_name)
						$this->idx->set($obj_type, $obj_name, $obj_key);
				}

				switch ($obj_type) {
				 case 'host': case 'program_status':
					if (!isset($obj['parents']))
						$this->glue_object($obj_key, $obj_type, $obj);
					break;
				 case 'timeperiod':
					if (!isset($obj['exclude']))
						$this->glue_object($obj_key, 'timeperiod', $obj);
					break;
				 case 'hostgroup': case 'servicegroup': case 'contactgroup':
					break;
				 default:
					if (isset($service_slaves[$obj_type]))
						$this->post_mangle_service_slave($obj_type, $obj);

					$this->glue_object($obj_key, $obj_type, $obj);
				}
				if ($obj)
					$obj_array[$obj_type][$obj_key] = $obj;

				$obj_type = $obj_name = false;
				$obj_key++;
				continue;
			}

			$k = $this->mangle_var_name($obj_type, $str[0]);
			if (!$k || !$this->is_allowed_var($obj_type, $k))
				continue;

			# if the variable is set without a value, it means
			# "remove whatever is set in the template", so we
			# do just that. Nagios really shouldn't write these
			# parameters to the objects.cache file, but we need
			# to handle it just the same.
			if (!isset($str[1])) {
				unset($obj[$k]);
				continue;
			}
			$v = $str[1];

			switch ($k) {
			 case 'members': case 'parents': case 'exclude':
				$obj[$k] = $v;
				continue;
			 case 'contacts': case 'contact_groups':
				$ary = preg_split("/[\t ]*,[\t ]*/", $v);
				foreach ($ary as $v) {
					$target_id = $this->idx->get($relation[$k], $v);
					$v_ary[$target_id] = $target_id;
				}
				$obj[$k] = $v_ary;
				unset($ary); unset($v_ary);
				continue;
			 default:
				if ($k{0} === '_' ||
					($obj_type === 'timeperiod' && !isset($tp_vars[$k])))
				{
					$obj['__custom'][$k] = $v;
					continue;
				}
				if (isset($relation[$k])) {
					# handle commands specially
					if ($relation[$k] === 'command' && strpos($v, '!') !== false) {
						$ary = explode('!', $v);
						$v = $ary[0];
						$obj[$k . '_args'] = $ary[1];
					}
					$v = $this->idx->get($relation[$k], $v);
				}

				$obj[$k] = $v;
			}
		}

		# mop up any leftovers
		$this->done_parsing_obj_type_objects($last_obj_type, $obj_array);

		if (!empty($obj_array)) {
			echo "obj_array is not empty\n";
			print_r(array_keys($obj_array));
		}

		return !$this->errors;
	}

	/**
	 * @name	glue_custom_vars
	 * @param	object type (string)
	 * @param	object id (integer)
	 * @param	array (custom variables, k => v style)
	 */
	function glue_custom_vars($obj_type, $obj_id, $custom)
	{
		$ret = true;

		if (empty($custom))
			return true;

		$esc_obj_type = $this->sql_escape_string($obj_type);
		$esc_obj_id = $this->sql_escape_string($obj_id);
		$purge_query = 'DELETE FROM custom_vars WHERE ' .
			"obj_type = $esc_obj_type AND obj_id = $esc_obj_id";

		$result = $this->sql_exec_query($purge_query);
		if (!$custom)
			return $result;

		$base_query = "INSERT INTO custom_vars VALUES(" .
			$esc_obj_type . "," . $esc_obj_id . ", ";

		foreach ($custom as $k => $v) {
			$query = $base_query . $this->sql_escape_string($k) . ", " .
				$this->sql_escape_string($v) . ")";

			$result = $this->sql_exec_query($query);
			if (!$result)
				$ret = false;
		}

		return $ret;
	}

	private function obj_name($obj_type, $obj)
	{
		if (isset($obj[$obj_type . '_name']))
			return $obj[$obj_type . '_name'];

		if ($obj_type === 'service')
			return "$obj[host_name];$obj[service_description]";

		return false;
	}

	/**
	 * Inserts a single object into the database
	 */
	function glue_object($obj_key, $obj_type, &$obj)
	{
		if (isset($this->conv_type[$obj_type]))
			$obj_type = $this->conv_type[$obj_type];

		if ($this->importing_status && $this->is_full_import) {
			# mark hosts and services as pending if they
			# haven't been checked and current_state = 0
			if ($obj_type === 'host' || $obj_type === 'service') {
				if ($obj['current_state'] == 0 && $obj['has_been_checked'] == 0)
					$obj['current_state'] = 6;
			}
		}

		# Some objects are converted into oblivion when we're
		# importing status.sav. We ignore those here.
		if (!$obj_type) {
			$obj = false;
			return;
		}

		$fresh = isset($obj['is a fresh one']);
		if ($fresh) {
			unset($obj['is a fresh one']);
		}

		if ($obj_type === 'host' || $obj_type === 'service') {
			$obj_name = $this->obj_name($obj_type, $obj);
			if (isset($this->imported[$obj_type][$obj_key]) &&
				$this->imported[$obj_type][$obj_key] !== $obj_name)
			{
				echo "overwriting $obj_type id $obj_key in \$this->imported\n";
				printf("%s -> %s\n", $this->imported[$obj_type][$obj_key], $obj_name);
				print_r($obj);
				exit(0);
			}
			$this->imported[$obj_type][$obj_key] = $this->obj_name($obj_type, $obj);
		}

		if (isset($this->obj_rel[$obj_type]))
			$spec = $this->obj_rel[$obj_type];

		if ($obj_type === 'program_status') {
			unset($obj['id']);
			$obj['instance_id'] = 0;
			$obj['instance_name'] = 'Local Nagios/Merlin instance';
			$obj['is_running'] = 1;
			# kludge to work around duplicate key error in Oracle
			$this->sql_exec_query("DELETE FROM $obj_type WHERE instance_id='0'");
		} else {
			if ($obj_type === 'comment_tbl') {
				# According to nagios/comments.h:
				# #define HOST_COMMENT 1
				# #define SERVICE_COMMENT 2
				# We can't use "empty()" here, or a service_description
				# of '0' will cause it to be considered a host comment.
				if (isset($obj['service_description']) && strlen($obj['service_description'])) {
					$obj['comment_type'] = 2;
				} else {
					unset($obj['service_description']);
					$obj['comment_type'] = 1;
				}
			} elseif ($obj_type === 'scheduled_downtime') {
				# According to nagios/common.h:
				# #define SERVICE_DOWNTIME 1
				# #define HOST_DOWNTIME 2
				# #define ANY_DOWNTIME 3
				# that last one is a bit weird...
				if (isset($obj['service_description']) && strlen($obj['service_description'])) {
					$obj['downtime_type'] = 1;
				} else {
					unset($obj['service_description']);
					$obj['downtime_type'] = 2;
				}
			}
			$obj['id'] = $obj_key;
		}

		# stash away custom variables so the normal object
		# handling code doesn't have to deal with it.
		$custom = false;
		if (isset($obj['__custom'])) {
			$custom = $obj['__custom'];
			unset($obj['__custom']);
		}

		# loop every variable in the object
		foreach($obj as $k => $v) {
			if(is_array($v)) {
				# a junction thingie
				unset($obj[$k]);
				$query = false;
				$junction = $this->get_junction_table_name($obj_type, $k);

				# set up the values and insert them
				foreach($v as $junc_part) {
					$other_obj_type = $spec[$k];

					# timeperiod_exclude or host_parents
					if ($other_obj_type === $obj_type)
						$other_obj_type = $k;

					$query = "INSERT INTO $junction " .
						"($obj_type, $other_obj_type) " .
						"VALUES($obj_key, $junc_part)";
					$this->sql_exec_query($query);
				}
				continue;
			}
		}

		if ((!$fresh && ($obj_type === 'host' || $obj_type === 'service'))
			|| ($obj_type === 'contact' && $this->importing_status))
		{
			$query = "UPDATE $obj_type SET ";
			$oid = $obj['id'];
			unset($obj['id']);
			$params = array();
			foreach ($obj as $k => $v) {
				$params[] = "$k = :$k";
			}
			// Clean hosts and services so they don't keep stale data
			if ($obj_type !== 'contact' && !$this->importing_status) {
				$columns = $this->columns_to_clean[$obj_type];

				$missing_columns = array_diff_key($columns, $obj);
				foreach ($missing_columns as $column => $_) {
					$query .= "$column = DEFAULT, ";
				}
			}
			$query .= join(", ", $params) . " WHERE id = $oid";
		} else {
			# all vars are properly mangled, so let's run the query
			if(isset($obj['id'])) {
				$this->sql_exec_query("DELETE FROM $obj_type WHERE id=".$obj['id']);
			}
			$target_vars = implode(', ', array_keys($obj));
			$target_values = ':'.implode(', :', array_keys($obj));
			$query = "INSERT INTO $obj_type ($target_vars) " .
				"VALUES($target_values)";
		}

		if (!$this->sql_exec_query($query, $obj)) {
			$this->errors++;
			return false;
		}

		# $obj is passed by reference, so we can release
		# it here now that we're done with it.
		$obj = false;

		$this->glue_custom_vars($obj_type, $obj_key, $custom);

		return true;
	}

	/**
	 * A wrapper for glue_objects
	 */
	function glue_objects(&$obj_list, $obj_type)
	{
		# no objects, so no glueing to be done
		if (empty($obj_list))
			return true;

		foreach($obj_list as $obj_key => &$obj)
			$this->glue_object($obj_key, $obj_type, $obj);
	}

	function is_oracle()
	{
		switch (strtolower($this->db_type)) {
		 case 'oci': case 'ocilib': case 'oracle':
			return true;
			break;
		}
		return false;
	}

	# get an error from the last result
	function sql_error()
	{
		$ei = $this->db->errorInfo();
		return($ei[2]);
	}

	# get error number of last result
	function sql_errno()
	{
		$ei = $this->db->errorInfo();
		return($ei[1]);
	}

	# fetch a single row to indexed array
	function sql_fetch_row(PDOStatement $resource) {
		 return $result ? $resource->fetch(PDO::FETCH_NUM) : NULL;
	}

	# fetch a single row to associative array
	function sql_fetch_array(PDOStatement $resource) {
		return $resource ? $resource->fetch(PDO::FETCH_ASSOC) : NULL;
	}

	function sql_escape_string($string)
	{
		# this->db->quote() should return "''" for an empty string
		$s = $this->db->quote($string);
		if ($s)
			return $s;

		# the oracle driver seems to sometimes have problems with
		# this for some reason, though I don't know why. We solve
		# it by escaping it ourselves in that case, using a sort
		# of oracle-compatible escape thing but only handling the
		# simple case of single quotes inside the quoted string.
		$this->db_quote_special = true;
		$s = str_replace("'", "''", $string);
		return "'$s'";
	}

	# execute an SQL query with error handling
	function sql_exec_query($query, $args=null)
	{
		if(empty($query))
			return(false);

		$this->total_queries++;

		# workaround for now
		if($this->db === false) {
			$this->gui_db_connect();
		}
		if($this->debug) {
			$fn = basename(__FILE__);
			echo $fn.": QUERY: [$query]\n";
		}
		if ($args) {
			$stmt = $this->db->prepare($query);
			$result = $stmt->execute($args);
		} else {
			$result = $this->db->query($query);
		}
		return($result);
	}

	// fetch complete results of a query with error checking
	// if a table named 'id' exists, resulting array is indexed by it
	function sql_fetch_result($resource)
	{
		$ret = false;
		$id = false;
		$i = 0;

		if(empty($resource)) {
			if($this->debug) echo "SQL ERROR: sql_fetch_result() called with empty resource\n";
			return(false);
		}

		while($row = $result->fetch(PDO::FETCH_ASSOC)) {
			$i++;
			if(isset($row['id'])) {
				$id = $row['id'];
				unset($row['id']);
			}
			else $id = $i;

			if(!empty($row)) {
				foreach($row as $k => $v) {
					if(empty($v) && $v !== 0 && $v !== '0') continue;
					$ret[$id][$k] = $v;
				}
			}
			else {
				$ret[$id] = false;
			}
		}
		return($ret);
	}

	# connects to and selects database. false on error, true on success
	function gui_db_connect()
	{
		if (!$this->db) {
			$this->db = MerlinPDO::db($this->db_type, $this->db_database, $this->db_user, $this->db_pass, $this->db_host, $this->db_port);
		}
		return $this->db;
	}
}
