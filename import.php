#!/usr/bin/php
<?php

require_once('object_importer.inc.php');

$imp = new nagios_object_importer;
$imp->db_type = 'mysql';
$imp->db_host = 'localhost';
$imp->db_user = 'merlin';
$imp->db_pass = 'merlin';
$imp->db_database = 'merlin';

if (PHP_SAPI !== 'cli') {
	$msg = "This program must be run from the command-line version of PHP\n";
	echo $msg;
	die($msg);
}

$progname = basename($argv[0]);
function usage($msg = false)
{
	global $progname;

	echo "Usage: $progname <options> --cache=/path/to/objects.cache\n";
	echo "\n";
	echo "--dbname   name of database to import to\n";
	echo "--dbuser   database username\n";
	echo "--dbhost   database host\n";
	echo "--dbpass   database password\n";
	echo "--dbtype   database type (mysql is the only supported for now)\n";
	echo "--cache    path to the objects.cache file to import\n";
	exit(1);
}

function read_nagios_cfg($cfgfile)
{
	if (!file_exists($cfgfile))
		return false;

	$cfg = false;
	$lines = explode("\n", file_get_contents($cfgfile));
	foreach ($lines as $line) {
		$line = trim($line);
		if (!strlen($line) || $line{0} === '#')
			continue;
		$ary = explode('=', $line);
		$k = $ary[0];
		$v = $ary[1];
		if (isset($cfg[$k])) {
			if (!is_array($cfg[$k]))
				$cfg[$k] = array($cfg[$k]);
			$cfg[$k][] = $v;
			continue;
		}
		$cfg[$k] = $v;
	}

	return $cfg;
}

if (PHP_SAPI !== 'cli') {
	usage("This program can only be run from the command-line\n");
}

$nagios_cfg = $dry_run = $cache = $status_log = false;
for ($i = 1; $i < $argc; $i++) {
	$arg = $argv[$i];
	if (substr($arg, 0, 2) !== '--') {
		$cache = $arg;
		continue;
	}
	$arg = substr($arg, 2);

	$opt = false;
	$optpos = strpos($arg, '=');
	if ($optpos !== false) {
		# must set 'opt' first
		$opt = substr($arg, $optpos + 1);
		$arg = substr($arg, 0, $optpos);
	} elseif ($i < $argc - 1) {
		$opt = $argc[++$i];
	} elseif ($arg !== 'dry-run') {
		usage("$arg requires an option\n");
	}
	switch ($arg) {
	 case 'dbname': $imp->db_database = $opt; break;
	 case 'dbuser': $imp->db_user = $opt; break;
	 case 'dbpass': $imp->db_pass = $opt; break;
	 case 'dbtype': $imp->db_type = $opt; break;
	 case 'dbhost': $imp->db_host = $opt; break;
	 case 'cache': $cache = $opt; break;
	 case 'status-log': $status_log = $opt; break;
	 case 'nagios-cfg': $nagios_cfg = $opt; break;
	 case 'dry-run': $dry_run = true; break;
	 default:
		usage("Unknown argument: $arg\n");
		break;
	}
}

if ($cache)
	echo "importing objects from $cache\n";

if (!$dry_run)
	$imp->import_objects_from_cache($cache);

if ($nagios_cfg && !$status_log) {
	$config = read_nagios_cfg($nagios_cfg);
	if (isset($config['status_file']))
		$status_log = $config['status_file'];
	elseif (isset($config['xsddefault_status_file']))
		$status_log = $config['xsddefault_status_file'];
}

if ($status_log) {
	echo "importing status from $status_log\n";
	if (!$dry_run)
		$imp->import_objects_from_cache($status_log);
}
