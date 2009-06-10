<?php

class nagios_object_importer
{
	public $instance_id = 0;

	public $db_type = 'mysql';
	public $db_host = 'localhost';
	public $db_user = '@@DBUSER@@';
	public $db_pass = '@@DBPASS@@';
	public $db_database = '@@DBNAME@@';
	protected $db = false;

	public $errors = 0;

	public $DEBUG = true;

	# internal object indexing cache
	private $idx_table = array();
	private $rev_idx_table = array();
	private $base_oid = array();
	private $imported = array();

	# denotes if we're importing status.sav or objects.cache
	private $importing_status = false;

	private $tables_to_truncate = array
		('command',
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
		 );

	# conversion table for variable names
	private $convert = array();
	private $conv_type = array
		('info' => false, 'program' => 'program_status',
		 'programstatus' => 'program_status',
		 'hoststatus' => 'host', 'servicestatus' => 'service',
		 'contactstatus' => 'contact',
		 'servicecomment' => 'comment', 'hostcomment' => 'comment');

	# allowed variables for each object
	private $allowed_vars = array();

	# object relations table used to determine db junction table names etc
	protected $Object_Relation = array();

	public function __construct()
	{
		$this->obj_rel['host'] =
			array('parents' => 'host',
				  'contacts' => 'contact',
				  'contact_groups' => 'contactgroup'
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
	}

	function get_junction_table_name($obj_type, $v_name)
	{
		$ref_obj_type = $this->obj_rel[$obj_type][$v_name];
		$ret = $obj_type . '_' . $ref_obj_type;

		if($v_name === 'members')
			$ret = $ref_obj_type . '_' . $obj_type;
		elseif($ref_obj_type === $obj_type)
			$ret = $obj_type . '_' . $v_name;

		return $ret;
	}

	function post_mangle_groups($group, &$obj_list)
	{
		if (empty($obj_list))
			return;

		$ref_type = str_replace('group', '', $group);

		foreach ($obj_list as $obj_key => &$obj) {
			if (!isset($obj['members']))
				continue;

			$ary = explode(',', $obj['members']);
			$v_ary = array(); # reset between each loop
			if ($group === 'servicegroup') {
				while ($srv = array_pop($ary)) {
					$host = array_pop($ary);
					$v_ary[] = $this->rev_idx_table['service']["$host;$srv"];
				}
			}
			else foreach ($ary as $mname) {
				if (is_array($this->rev_idx_table[$ref_type][$mname])) {
					echo "Rev_Index_Table[$ref_type][$mname] is an array!\n";
					exit(1);
				}
				$v_ary[] = $this->rev_idx_table[$ref_type][$mname];
			}
			$obj['members'] = $v_ary;
			$this->glue_object($obj_key, $group, $obj);
		}
	}

	function post_mangle_self_ref($obj_type, &$obj_list)
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
			$ary = explode(',', $obj[$k]);
			$obj[$k] = array();
			foreach ($ary as $v)
				$obj[$k][] = $this->rev_idx_table[$obj_type][$v];
		}
	}

	function post_mangle_service_slave($obj_type, &$obj)
	{
		$srv = $obj['host_name'] . ';' . $obj['service_description'];
		$obj['service'] = $this->rev_idx_table['service'][$srv];
		unset($obj['host_name']);
		unset($obj['service_description']);

		if ($obj_type === 'servicedependency') {
			$srv = $obj['dependent_host_name'] . ';' . $obj['dependent_service_description'];
			$obj['dependent_service'] = $this->rev_idx_table['service'][$srv];
			unset($obj['dependent_host_name']);
			unset($obj['dependent_service_description']);
		}
	}

	/**
	 * Invoked when we encounter a new type of object in objects.cache
	 * Since objects are grouped by type in that file, this means we're
	 * done entirely parsing the type of objects passed in $obj_type
	 */
	function done_parsing_obj_type_objects($obj_type, &$obj_array)
	{
		if ($obj_type === false || empty($obj_array))
			return true;

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

	private function preload_object_index($obj_type, $query)
	{
		$this->idx_table[$obj_type] = array();
		$this->rev_idx_table[$obj_type] = array();
		$result = $this->sql_exec_query($query);
		while ($row = $this->sql_fetch_row($result)) {
			$this->idx_table[$obj_type][$row[0]] = $row[1];
			$this->rev_idx_table[$obj_type][$row[1]] = $row[0];
		}
		$result = $this->sql_exec_query("SELECT MAX(id) FROM $obj_type");
		$row = $this->sql_fetch_row($result);
		if ($row) {
			$this->base_oid[$obj_type] = $row[0] + 1;
		} else {
			$this->base_oid[$obj_type] = 1;
		}
	}

	private function purge_old_objects()
	{
		foreach ($this->imported as $obj_type => $ids) {
			$query = "DELETE FROM $obj_type WHERE id NOT IN (";
			$query .= join(', ', array_keys($ids)) . ')';
			$result = $this->sql_exec_query($query);
		}
	}

	private function is_allowed_var($obj_type, $k)
	{
		if (!$this->importing_status)
			return true;

		if ($obj_type === 'info')
			return false;

		if (!isset($this->allowed_vars[$obj_type])) {
			$result = $this->sql_exec_query("describe $obj_type");
			if (!$result)
				return false;

			while ($row = $this->sql_fetch_row($result)) {
				$this->allowed_vars[$obj_type][$row[0]] = $row[0];
			}
		}

		return isset($this->allowed_vars[$obj_type][$k]);
	}

	private function mangle_var_name($obj_type, $k)
	{
		if (!$this->importing_status)
			return $k;

		if (empty($k)) {
			echo("Found empty \$k with obj_type $obj_type\n");
			echo var_dump($k);
			exit(1);
		}

		if (isset($this->convert[$obj_type][$k]))
			return $this->convert[$obj_type][$k];
		if ($obj_type === 'program_status') {
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
		}
		return $k;
	}

	// pull all objects from objects.cache
	function import_objects_from_cache($object_cache = false)
	{
		$last_obj_type = false;
		$obj_type = false;
		$obj_key = 1;

		if (!$object_cache)
			$object_cache = '/opt/monitor/var/objects.cache';

		foreach($this->tables_to_truncate as $table)
			$this->sql_exec_query("TRUNCATE $table");

		$this->preload_object_index('host', 'SELECT id, host_name FROM host');
		$this->preload_object_index('service', "SELECT id, CONCAT(host_name, ';', service_description) FROM service");

		# service slave objects are handled separately
		$service_slaves =
			array('serviceescalation' => 1,
				  'servicedependency' => 1);

		$fh = fopen($object_cache, "r");
		if (!$fh)
			return false;

		# fetch 'real' timeperiod variables so we can exclude all
		# others as custom variables
		$result = $this->sql_exec_query('describe timeperiod');
		$tp_vars = array();
		while ($ary = $this->sql_fetch_array($result)) {
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

				if ($obj_type === 'info' || $obj_type === 'program') {
					$this->importing_status = true;
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
					$obj_key = 1;
					if (isset($this->base_oid[$obj_type]))
						$obj_key = $this->base_oid[$obj_type]++;
				}
				$obj = array();
				continue;
			}

			// we're inside an object now, so check for closure and tag if so
			$str = preg_split("/[\t=]/", $line, 2);

			// end of object? check type and populate index table
			if ($str[0] === '}') {
				$obj_name = false;
				if (isset($obj[$obj_type . '_name']))
					$obj_name = $obj[$obj_type . '_name'];
				elseif($obj_type === 'service')
					$obj_name = "$obj[host_name];$obj[service_description]";

				if ($obj_name) {
					# use pre-loaded object id if available
					if (isset($this->rev_idx_table[$obj_type][$obj_name])) {
						$obj_key = $this->rev_idx_table[$obj_type][$obj_name];
					}
					else {
						$this->rev_idx_table[$obj_type][$obj_name] = $obj_key;
						$obj['is a fresh one'] = true;
					}
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
			$v = $str[1];

			switch ($k) {
			 case 'members': case 'parents': case 'exclude':
				$obj[$k] = $v;
				continue;
			 case 'contacts': case 'contact_groups':
				$ary = explode(',', $v);
				foreach ($ary as $v) {
					$target_id = $this->rev_idx_table[$relation[$k]][$v];
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
					$v = $this->rev_idx_table[$relation[$k]][$v];
				}

				$obj[$k] = $v;
			}
		}

		# mop up any leftovers
		$this->done_parsing_obj_type_objects($last_obj_type, $obj_array);

		# and remove 'dead' objects from the database
		$this->purge_old_objects();

		if (!empty($obj_array)) {
			echo "obj_array is not empty\n";
			print_r(array_keys($obj_array));
		}
		assert('empty($obj_array)');

		if(!isset($_SERVER['REMOTE_USER'])) $user = 'local';
		else $user = $_SERVER['REMOTE_USER'];

		if (!$this->errors) {
			$this->sql_exec_query("REPLACE INTO gui_action_log (user, action) " .
						   "VALUES('" .
						   mysql_escape_string($user) . "', 'import')");
			return true;
		}

		return false;
	}

	function import_objects_if_new_cache($object_cache = false)
	{
		$import_time = 0;

		if (!$object_cache)
			$object_cache = '/opt/monitor/var/objects.cache';

		$result = $this->sql_exec_query
			('SELECT UNIX_TIMESTAMP(time) AS time ' .
			 'FROM gui_action_log ' .
			 'WHERE action="import" ' .
			 'ORDER BY time DESC LIMIT 1');

		$row = sql_fetch_row($result);
		if(!empty($row[0])) $import_time = $row[0];

		$cache_time = filemtime($object_cache);
		if($cache_time > $import_time) {
			return($this->import_objects_from_cache($object_cache));
		}

		return(0);
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
			"obj_type = '$esc_obj_type' AND obj_id = '$esc_obj_id'";

		$result = $this->sql_exec_query($purge_query);
		if (!$custom)
			return $result;

		$base_query = "INSERT INTO custom_vars VALUES('" .
			$esc_obj_type . "','" . $esc_obj_id . "', '";

		foreach ($custom as $k => $v) {
			$query = $base_query . $this->sql_escape_string($k) . "','" .
				$this->sql_escape_string($v) . "')";

			$result = $this->sql_exec_query($query);
			if (!$result)
				$ret = false;
		}

		return $ret;
	}

	/**
	 * Inserts a single object into the database
	 */
	function glue_object($obj_key, $obj_type, &$obj)
	{
		if (isset($this->conv_type[$obj_type]))
			$obj_type = $this->conv_type[$obj_type];

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
			$this->imported[$obj_type][$obj_key] = true;
		}
		if (isset($this->obj_rel[$obj_type]))
			$spec = $this->obj_rel[$obj_type];

		if ($obj_type === 'program_status') {
			unset($obj['id']);
			$obj['instance_id'] = 0;
			$obj['instance_name'] = 'Local Nagios/Merlin instance';
			$obj['is_running'] = 1;
		} else {
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

			if (!is_numeric($v))
				$obj[$k] = '\'' . $this->sql_escape_string($v) . '\'';
			else
				$obj[$k] = '\'' . $v . '\'';
		}

		if ((!$fresh && ($obj_type === 'host' || $obj_type === 'service'))
			|| ($obj_type === 'contact' && $this->importing_status))
		{
			$query = "UPDATE $obj_type SET ";
			$oid = $obj['id'];
			unset($obj['id']);
			$params = array();
			foreach ($obj as $k => $v) {
				$params[] = "$k = $v";
			}
			$query .= join(", ", $params) . " WHERE id = $oid";
		} else {
			# all vars are properly mangled, so let's run the query
			$target_vars = implode(',', array_keys($obj));
			$target_values = implode(',', array_values($obj));
			$query = "REPLACE INTO $obj_type($target_vars) " .
				"VALUES($target_values)";
		}
		if (!$this->sql_exec_query($query)) {
			$this->errors++;
			return false;
		}

		$this->glue_custom_vars($obj_type, $obj_key, $custom);

		# $obj is passed by reference, so we can release
		# it here now that we're done with it.
		$obj = false;

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

	# get an error from the last result
	function sql_error()
	{
		return(mysql_error($this->db));
	}

	# get error number of last result
	function sql_errno()
	{
		return(mysql_errno($this->db));
	}

	# fetch a single row to indexed array
	function sql_fetch_row($resource) {
		return(mysql_fetch_row($resource));
	}

	# fetch a single row to associative array
	function sql_fetch_array($resource) {
		return(mysql_fetch_array($resource, MYSQL_ASSOC));
	}

	function sql_escape_string($string)
	{
		return mysql_real_escape_string($string);
	}

	# execute an SQL query with error handling
	function sql_exec_query($query)
	{
		if(empty($query))
			return(false);

		# workaround for now
		if($this->db === false) {
			$this->gui_db_connect();
		}

		$result = mysql_query($query, $this->db);
		if($result === false) {
			echo "SQL query failed with the following error message;<br />\n" .
				mysql_error() . "<br />\n";
			if($this->DEBUG) echo "Query was;<br />\n<b>$query</b><br />\n";
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
			if($this->DEBUG) echo "SQL ERROR: sql_fetch_result() called with empty resource\n";
			return(false);
		}

		while($row = $this->sql_fetch_array($resource)) {
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
		if($this->db_type !== 'mysql') {
			die("Only mysql is supported as of yet.<br />\n");
		}

		$this->db = mysql_connect
			($this->db_host, $this->db_user, $this->db_pass);

		if ($this->db === false)
			return(false);

		return mysql_select_db($this->db_database);
	}
}
