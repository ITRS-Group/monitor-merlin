#!/usr/bin/php
<?php
/*
 * Copyright(C) op5 AB
 * All rights reserved.
 */

$DEBUG = true;

$gui_db_opt['type'] = 'mysql'; // mysql is the only one supported for now.
$gui_db_opt['host'] = 'localhost';
$gui_db_opt['user'] = '@@DBUSER@@';
$gui_db_opt['passwd'] = '@@DBPASS@@';
$gui_db_opt['database'] = '@@DBNAME@@';
$gui_dbh = false; // database resource

# internal object indexing cache
$Object_Index_Table = false;

# List of tables to delete when importing config.
$db_object_tables = array('command',
				   		  'contact',
						  'contact_contactgroup',
						  'contactgroup',
						  'host',
						  'host_contact',
						  'host_contactgroup',
						  'host_hostgroup',
						  'host_parents',
						  'hostdependency',
						  'hostescalation',
						  'hostescalation_contact',
						  'hostescalation_contactgroup',
						  'hostgroup',
						  'service',
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

# object relations table used to determine db junction table names etc
$Object_Relation['host'] =
  array('parents' => 'host',
		'check_command' => 'command',
		'notification_period' => 'timeperiod',
		'contacts' => 'contact',
		'contact_groups' => 'contactgroup'
		);

$Object_Relation['hostgroup'] =
  array('members' => 'host');

$Object_Relation['service'] =
  array('host_name' => 'host',
		'contacts' => 'contact',
		'contact_groups' => 'contactgroup',
		'notification_period' => 'timeperiod',
		'check_period' => 'timeperiod',
		'check_command' => 'command'
		);

$Object_Relation['contact'] =
  array('host_notification_period' => 'timeperiod',
		'service_notification_period' => 'timeperiod',
		'host_notification_commands' => 'command',
		'service_notification_commands' => 'command'
		);

$Object_Relation['contactgroup'] =
  array('members' => 'contact');

$Object_Relation['servicedependency'] =
  array('host_name' => 'host',
		'dependent_host_name' => 'host',
		'dependency_period' => 'timeperiod',
		);

$Object_Relation['serviceescalation'] =
  array('host_name' => 'host',
		'contacts' => 'contact',
		'contact_groups' => 'contactgroup',
		'escalation_period' => 'timeperiod',
		);

$Object_Relation['servicegroup'] =
  array('members' => 'service');

$Object_Relation['hostdependency'] =
  array('host_name' => 'host',
		'dependent_host_name' => 'host',
		'dependency_period' => 'timeperiod');

$Object_Relation['hostescalation'] =
  array('host_name' => 'host',
		'contact_groups' => 'contactgroup',
		'escalation_period' => 'timeperiod',
		'contacts' => 'contact',
		'contact_groups' => 'contactgroup',
		);

$Object_Relation['timeperiod'] =
	array('exclude' => 'timeperiod');

function get_junction_table_name($obj_type, $v_name) {
	global $Object_Relation;

	$ref_obj_type = $Object_Relation[$obj_type][$v_name];
	$ret = $obj_type . '_' . $ref_obj_type;

	if($v_name === 'members')
		$ret = $ref_obj_type . '_' . $obj_type;
	elseif($ref_obj_type === $obj_type)
		$ret = $obj_type . '_' . $v_name;

	return $ret;
}

$Rev_Index_Table = array();
$import_errors = 0;
function post_mangle_groups($group, &$obj_list)
{
	global $Rev_Index_Table, $import_errors;

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
				$host = $Rev_Index_Table['host'][array_pop($ary)];
				$v_ary[] = $Rev_Index_Table['service']["$host;$srv"];
			}
		}
		else foreach ($ary as $mname) {
			if (is_array($Rev_Index_Table[$ref_type][$mname])) {
				echo "Rev_Index_Table[$ref_type][$mname] is an array!\n";
				exit(1);
			}
			$v_ary[] = $Rev_Index_Table[$ref_type][$mname];
		}
		$obj['members'] = $v_ary;
		glue_object($obj_key, $group, $obj);
	}
}

function post_mangle_self_ref($obj_type, &$obj_list)
{
	global $Rev_Index_Table;

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
			$obj[$k][] = $Rev_Index_Table[$obj_type][$v];
	}
}

function post_mangle_service_slave($obj_type, &$obj)
{
	global $Rev_Index_Table;

	$srv = $obj['host_name'] . ';' . $obj['service_description'];
	$obj['service'] = $Rev_Index_Table['service'][$srv];
	unset($obj['host_name']);
	unset($obj['service_description']);

	if ($obj_type === 'servicedependency') {
		$srv = $obj['dependent_host_name'] . ';' . $obj['dependent_service_description'];
		$obj['dependent_service'] = $Rev_Index_Table['service'][$srv];
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
		post_mangle_self_ref($obj_type, $obj_array[$obj_type]);
		glue_objects($obj_array[$obj_type], $obj_type);
		unset($obj_array[$obj_type]);
		# fallthrough, although no-op for timeperiods
	 case 'service': case 'contact':
		$group = $obj_type . 'group';
		if (isset($obj_array[$group])) {
			post_mangle_groups($group, $obj_array[$group]);
			unset($obj_array[$group]);
		}
		break;
	}

	return true;
}

// pull all objects from objects.cache
function import_objects_from_cache($object_cache = '/opt/monitor/var/objects.cache')
{
	global $Object_Relation;
	global $db_object_tables; /* Array of db-tbls to del when importing  conf */
	global $Rev_Index_Table;
	global $import_errors;

	$last_obj_type = false;
	$obj_type = false;
	$obj_key = 1;

	foreach($db_object_tables as $table)
		sql_exec_query("TRUNCATE $table");

	# service slave objects are handled separately
	$service_slaves =
		array('serviceescalation' => 1,
		      'servicedependency' => 1);

	$fh = fopen($object_cache, "r");
	if (!$fh)
		return false;

	# fetch 'real' timeperiod variables so we can exclude all
	# others as custom variables
	$result = sql_exec_query('describe timeperiod');
	$tp_vars = array();
	while ($ary = sql_fetch_array($result)) {
		$tp_vars[$ary['Field']] = $ary['Field'];
	}
	unset($result);

	while (!feof($fh)) {
		$line = trim(fgets($fh));

		if(empty($line) || $line{0} === '#') continue;
		if(!$obj_type) {
			$str = explode(' ', $line, 3);
			$obj_type = $str[1];

			# get rid of objects as early as we can
			if($obj_type !== $last_obj_type) {
				if (isset($Object_Relation[$obj_type]))
					$relation = $Object_Relation[$obj_type];
				else
					$relation = false;

				done_parsing_obj_type_objects($last_obj_type, $obj_array);
				$last_obj_type = $obj_type;
				$obj_key = 1;
			}
			$obj = array();
			continue;
		}

		// we're inside an object now, so check for closure and tag if so
		$str = explode("\t", $line);

		// end of object? check type and populate index table
		if($str[0] === '}') {
			if (isset($obj[$obj_type . '_name']))
				$obj_name = $obj[$obj_type . '_name'];
			elseif($obj_type === 'service')
				$obj_name = "$obj[host_name];$obj[service_description]";

			if($obj_name) {
				$Rev_Index_Table[$obj_type][$obj_name] = $obj_key;
			}
			switch ($obj_type) {
			 case 'host':
				if (!isset($obj['parents']))
					glue_object($obj_key, 'host', $obj);
				break;
			 case 'timeperiod':
				if (!isset($obj['exclude']))
					glue_object($obj_key, 'timeperiod', $obj);
				break;
			 case 'hostgroup': case 'servicegroup': case 'contactgroup':
				break;
			 default:
				if (isset($service_slaves[$obj_type]))
					post_mangle_service_slave($obj_type, $obj);

				glue_object($obj_key, $obj_type, $obj);
			}
			if ($obj)
				$obj_array[$obj_type][$obj_key] = $obj;

			$obj_type = $obj_name = false;
			$obj_key++;
			continue;
		}

		$k = $str[0];
		$v = $str[1];

		switch ($k) {
		 case 'members': case 'parents': case 'exclude':
			$obj[$k] = $v;
			continue;
		 case 'contacts': case 'contact_groups':
			$ary = explode(',', $v);
			foreach ($ary as $v) {
				$target_id = $Rev_Index_Table[$relation[$k]][$v];
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
				$v = $Rev_Index_Table[$relation[$k]][$v];
			}

			$obj[$k] = $v;
		}
	}

	# mop up any leftovers
	done_parsing_obj_type_objects($last_obj_type, $obj_array);

	assert('empty($obj_array)');

	if(!isset($_SERVER['REMOTE_USER'])) $user = 'local';
	else $user = $_SERVER['REMOTE_USER'];

	if (!$import_errors) {
		sql_exec_query("REPLACE INTO gui_action_log (user, action) " .
		               "VALUES('" .
		                mysql_escape_string($user) . "', 'import')");
		return true;
	}

	return false;
}

function import_objects_if_new_cache($object_cache = '/opt/monitor/var/objects.cache')
{
	global $main_config;
	global $gui_dbh;

	$import_time = 0;

	$result = sql_exec_query('SELECT UNIX_TIMESTAMP(time) AS time ' .
							 'FROM gui_action_log ' .
							 'WHERE action="import" ' .
							 'ORDER BY time DESC LIMIT 1');

	$row = sql_fetch_row($result);
	if(!empty($row[0])) $import_time = $row[0];

	$cache_time = filemtime($object_cache);
	if($cache_time > $import_time) {
		return(import_objects_from_cache());
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

	$esc_obj_type = sql_escape_string($obj_type);
	$esc_obj_id = sql_escape_string($obj_id);
	$purge_query = 'DELETE FROM custom_vars WHERE ' .
		"obj_type = '$esc_obj_type' AND obj_id = '$esc_obj_id'";

	$result = sql_exec_query($purge_query);
	if (!$custom)
		return $result;

	$base_query = "INSERT INTO custom_vars VALUES('" .
		$esc_obj_type . "','" . $esc_obj_id . "', '";

	foreach ($custom as $k => $v) {
		$query = $base_query . sql_escape_string($k) . "','" .
			sql_escape_string($v) . "')";

		$result = sql_exec_query($query);
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
	global $Object_Relation, $import_errors;

	if (isset($Object_Relation[$obj_type]))
		$spec = $Object_Relation[$obj_type];

	$obj['id'] = $obj_key;

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
			$junction = get_junction_table_name($obj_type, $k);

			# set up the values and insert them
			foreach($v as $junc_part) {
				$other_obj_type = $spec[$k];

				# timeperiod_exclude or host_parents
				if ($other_obj_type === $obj_type)
					$other_obj_type = $k;

				$query = "INSERT INTO $junction " .
					"($obj_type, $other_obj_type) " .
					"VALUES($obj_key, $junc_part)";
				sql_exec_query($query);
			}
			continue;
		}

		if (!is_numeric($v))
			$obj[$k] = '\'' . sql_escape_string($v) . '\'';
	}

	# all vars are properly mangled, so let's run the query
	$target_vars = implode(',', array_keys($obj));
	$target_values = implode(',', array_values($obj));
	$query = "REPLACE INTO $obj_type($target_vars) " .
		"VALUES($target_values)";
	if (!sql_exec_query($query)) {
		$import_errors++;
		return false;
	}

	glue_custom_vars($obj_type, $obj_key, $custom);

	# $obj is passed by reference, so we can release
	# it here now that we're done with it.
	$obj = false;

	return true;
}

/**
 * A wrapper for glue_objects
 */
function glue_objects(&$obj_list, $obj_type) {
	# no objects, so no glueing to be done
	if (empty($obj_list))
		return true;

	foreach($obj_list as $obj_key => &$obj)
		glue_object($obj_key, $obj_type, $obj);
}

# generate index of the required object type, and return it.
# we cache internally for obvious reasons.
$Object_Index_Table = false;
function get_object_index_table($obj_type)
{
	global $Object_Index_Table;

	# if this index table is cached, we return immediately
	if(isset($Object_Index_Table[$obj_type])) {
		return($Object_Index_Table[$obj_type]);
	}

	$simple_objects = array('host', 'command', 'contact', 'contactgroup',
							'servicegroup', 'hostgroup', 'timeperiod', 'FILE');
	$template_objects = array('host_template',
							  'contact_template',
							  'service_template');

	# basically get_token_values for all objects that will require it.
	if(in_array($obj_type, $simple_objects) ||
	   in_array($obj_type, $template_objects)) {
		if(in_array($obj_type, $simple_objects)) {
			$query = "SELECT id, ${obj_type}_name " .
			  		 "FROM $obj_type " .
					 "WHERE ${obj_type}_name != '' " .
					 "ORDER BY ${obj_type}_name";
		} else {
			$query = "SELECT id, name " .
					 "FROM " . substr($obj_type, 0, -9) . " " .
					 "WHERE name != '' ORDER BY name";
		}
		$result = sql_exec_query($query);

		if(!$result) {
			bug("SQL ERROR while populating index table for object type $obj_type!<br>\n".
				mysql_error($result) . "<br>\n",
				false, "Query was $query");
			return(false);
		}
		while($row = mysql_fetch_row($result)) {
			$Object_Index_Table[$obj_type][$row[0]] = $row[1];
		}
	} elseif($obj_type === 'service') {
		# services requires a rather specially formed SQL query
		# so we handle it separately.
		$result = sql_exec_query
		  ("SELECT service.id, host.host_name, service.service_description " .
		   "FROM host, service " .
		   "WHERE host.id = service.host_name AND service.service_description != '' " .
		   "ORDER BY host.host_name, service.service_description");

		$srv = sql_fetch_result($result);
		foreach($srv as $obj_key => $obj) {
			$Object_Index_Table['service'][$obj_key] =
		  $obj['host_name'] . ';' . $obj['service_description'];
		}
		return($Object_Index_Table['service']);
	}

	return(false);
}

function get_junction_data($obj_type, $obj_key, $obj_var, $jnc_var)
{
	$jnc_data = false;

	$table = get_junction_table_name($obj_type, $obj_var);

	# deal with self-referencing objects (host_parents, for now)
	$query = false;
	if($obj_type === $jnc_var) $jnc_var = $obj_var;
	if($obj_type === 'host' && $obj_var === 'children') {
		$query = "SELECT host FROM host_parents WHERE parents=" .
		  mysql_escape_string($obj_key);
	}

	if($query === false) {
		$query = "SELECT $jnc_var FROM $table WHERE " .
		  mysql_escape_string($obj_type) . "=" .
		  mysql_escape_string($obj_key);
	}
	$result = sql_exec_query($query);
	while($row = mysql_fetch_row($result)) {
		$jnc_data[$row[0]] = $row[0];
	}

	return $jnc_data;
}

function get_parents($host)
{
	$parents = array();

	if(!is_int($host)) {
		# we need a host id, but it might not be present to the caller
		$result = sql_exec_query('SELECT id FROM host WHERE host_name = \'' .
								 mysql_escape_string($host) . '\'');
		$row = sql_fetch_row($result);
		$host = $row[0];
	}

	$query = "SELECT DISTINCT host_name from host, host_parents " .
	  "WHERE host.id = host_parents.parents " .
	  "AND host_parents.host = $host";

	$result = sql_exec_query($query);

	while($row = sql_fetch_row($result)) {
		$parents[] = $row[0];
	}

	return($parents);
}


function get_hostgroups($host) {
	$hostgroups = array();
	$result = sql_exec_query
	  ("SELECT DISTINCT hostgroup_name FROM hostgroup, host_hostgroup " .
	   "WHERE host_hostgroup.host = $host " .
	   "AND hostgroup.id = host_hostgroup.hostgroup");
	while($row = sql_fetch_row($result)) {
		$hostgroups[] = $row[0];
	}

	return($hostgroups);
}

# get an error from the last result
function sql_error() {
	global $gui_dbh;
	return(mysql_error($gui_db));
}

# get error number of last result
function sql_errno() {
	global $gui_dbh;
	return(mysql_errno($gui_db));
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
function sql_exec_query($query) {
	global $DEBUG;
	global $gui_dbh;

	if(empty($query)) return(false);

	# workaround for now
	if($gui_dbh === false) {
		gui_db_connect();
	}

	$result = mysql_query($query, $gui_dbh);
	if($result === false) {
		echo "SQL query failed with the following error message;<br />\n" .
		  mysql_error() . "<br />\n";
		if($DEBUG) echo "Query was;<br />\n<b>$query</b><br />\n";
	}

	return($result);
}

// fetch complete results of a query with error checking
// if a table named 'id' exists, resulting array is indexed by it
function sql_fetch_result($resource) {
	global $db_type;
	global $DEBUG;

	$ret = false;
	$id = false;
	$i = 0;

	if(empty($resource)) {
		if($DEBUG) echo "SQL ERROR: sql_fetch_result() called with empty resource\n";
		return(false);
	}

	while($row = sql_fetch_array($resource)) {
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
	global $gui_dbh;
	global $gui_db_opt;

	if($gui_db_opt['type'] !== 'mysql') {
		die("Only mysql is supported as of yet.<br />\n");
	}

	$gui_dbh = mysql_connect($gui_db_opt['host'],
							 $gui_db_opt['user'],
							 $gui_db_opt['passwd']);

	if ($gui_dbh === false)
		return(false);

	return mysql_select_db($gui_db_opt['database']);
}

import_objects_from_cache();
