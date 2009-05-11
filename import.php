#!/usr/bin/php
<?php

require_once('object_importer.inc.php');

$imp = new nagios_object_importer;
$imp->db_type = 'mysql';
$imp->db_host = 'localhost';
$imp->db_user = 'merlin';
$imp->db_pass = 'merlin';
$imp->db_database = 'merlin';
$imp->import_objects_from_cache();
