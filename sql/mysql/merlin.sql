/*!40101 SET SQL_MODE="NO_AUTO_VALUE_ON_ZERO" */;
--
-- Database design for the merlin database
--

DELIMITER $$
DROP PROCEDURE IF EXISTS set_initial_db_version $$
CREATE PROCEDURE set_initial_db_version ()
BEGIN
	DECLARE VersionExists INT;
	SET VersionExists=0 ;

	SELECT count(*) INTO VersionExists from db_version;
	IF NOT (VersionExists > 0) THEN INSERT INTO db_version (version) VALUES (2);
	END IF;
END $$
DELIMITER ;

DROP TABLE IF EXISTS timeperiod;
CREATE TABLE timeperiod(
	instance_id				int NOT NULL DEFAULT 0,
	id						INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
	timeperiod_name			VARCHAR(255) NOT NULL,
	alias					VARCHAR(160) NOT NULL,
	sunday					VARCHAR(255),
	monday					VARCHAR(255),
	tuesday					VARCHAR(255),
	wednesday				VARCHAR(255),
	thursday				VARCHAR(255),
	friday					VARCHAR(255),
	saturday				VARCHAR(255)
) COLLATE latin1_general_cs;
CREATE UNIQUE INDEX t_timeperiod_name ON timeperiod(timeperiod_name);

-- junction table for timeperiod<->exclude
DROP TABLE IF EXISTS timeperiod_exclude;
CREATE TABLE timeperiod_exclude(
	timeperiod	INT NOT NULL,
	exclude		INT NOT NULL
) COLLATE latin1_general_cs;

-- custom variables
DROP TABLE IF EXISTS custom_vars;
CREATE TABLE custom_vars(
	obj_type	VARCHAR(30) NOT NULL,
	obj_id		INT NOT NULL,
	variable	VARCHAR(100),
	value		VARCHAR(255)
)  COLLATE latin1_general_cs;
-- No single object can have multiple variables named the same
CREATE UNIQUE INDEX cv_objvar ON custom_vars(obj_type, obj_id, variable);

-- gui <=> webconfig db scheme cross-pollination ends here

CREATE TABLE IF NOT EXISTS db_version (
  version int(11)
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

call set_initial_db_version();

-- Obsoleted tables
DROP TABLE IF EXISTS hostextinfo;
DROP TABLE IF EXISTS serviceextinfo;
DROP TABLE IF EXISTS gui_action_log;
DROP TABLE IF EXISTS gui_access;
DROP TABLE IF EXISTS command;
DROP TABLE IF EXISTS host;
DROP TABLE IF EXISTS host_parents;
DROP TABLE IF EXISTS host_contact;
DROP TABLE IF EXISTS host_contactgroup;
DROP TABLE IF EXISTS host_hostgroup;
DROP TABLE IF EXISTS hostgroup;
DROP TABLE IF EXISTS service;
DROP TABLE IF EXISTS service_contact;
DROP TABLE IF EXISTS service_contactgroup;
DROP TABLE IF EXISTS service_servicegroup;
DROP TABLE IF EXISTS servicegroup;
DROP TABLE IF EXISTS servicedependency;
DROP TABLE IF EXISTS serviceescalation;
DROP TABLE IF EXISTS serviceescalation_contact;
DROP TABLE IF EXISTS serviceescalation_contactgroup;
DROP TABLE IF EXISTS hostdependency;
DROP TABLE IF EXISTS hostescalation;
DROP TABLE IF EXISTS hostescalation_contact;
DROP TABLE IF EXISTS hostescalation_contactgroup;
DROP TABLE IF EXISTS contact_access;
DROP TABLE IF EXISTS program_status;
DROP TABLE IF EXISTS downtime;
DROP TABLE IF EXISTS scheduled_downtime;
DROP TABLE IF EXISTS comment;
DROP TABLE IF EXISTS comment_tbl;
DROP TABLE IF EXISTS contact;
DROP TABLE IF EXISTS contact_contactgroup;
DROP TABLE IF EXISTS contactgroup;

--
-- Tables not automagically re-created on every upgrade must
-- come below this, since they generate errors during upgrades
-- (but not on fresh installs) due to index naming conflicts.
-- These errors are harmless though, so we ignore them and just
-- plow on anyway.
--
CREATE TABLE IF NOT EXISTS notification(
	instance_id				int NOT NULL DEFAULT 0,
	id						int NOT NULL PRIMARY KEY AUTO_INCREMENT,
	notification_type		int,
	start_time				int(11),
	end_time				int(11),
	contact_name			varchar(255),
	host_name				varchar(255),
	service_description		varchar(255),
	command_name			varchar(255),
	reason_type				int,
	state					int,
	output					text,
	ack_author				varchar(255),
	ack_data				text,
	escalated				int,
	contacts_notified		int
) COLLATE latin1_general_cs;

--
-- This table is not automagically recreated every time we upgrade
--
CREATE TABLE IF NOT EXISTS report_data (
  timestamp int(11) NOT NULL default '0',
  event_type int(11) NOT NULL default '0',
  flags int(11),
  attrib int(11),
  host_name varchar(255) default '',
  service_description varchar(255) default '',
  state int(2) NOT NULL default '0',
  hard int(2) NOT NULL default '0',
  retry int(5) NOT NULL default '0',
  downtime_depth int(11),
  output text
) ENGINE=MyISAM DEFAULT CHARSET=latin1 COLLATE latin1_general_cs;


CREATE TABLE IF NOT EXISTS report_data_extras LIKE report_data;

--
-- This table is not automagically recreated every time we upgrade
-- and logging to it is not enabled by default. Repairing it if it
-- breaks, or pre-populating it with data, is impossible since Nagios
-- doesn't store historical performance data anywhere standard.
--
CREATE TABLE IF NOT EXISTS perfdata(
	timestamp  int(11) NOT NULL,
	host_name  varchar(255) NOT NULL,
	service_description varchar(255),
	perfdata TEXT NOT NULL
);

--
-- When doing a yum upgrade, there are usually two restarts of merlin with the
-- db wipe inbetween. Thus, don't recreate this, as that would render this
-- table useless.
--
-- Use a memory table, as it should be emptied on, say, power outages.
--
CREATE TABLE IF NOT EXISTS merlin_importer(
  pid int DEFAULT NULL
) ENGINE=MEMORY;

--
-- Funky table for keeping track of renames. We'll want to do this during
-- scheduled monitoring downtimes, as it could take an eternity, and we'll
-- want this to be done in bulk to save time.
--

CREATE TABLE IF NOT EXISTS rename_log(
  id INT PRIMARY KEY AUTO_INCREMENT,
  from_host_name VARCHAR(255),
  from_service_description VARCHAR(255) DEFAULT NULL,
  to_host_name VARCHAR(255),
  to_service_description VARCHAR(255) DEFAULT NULL
) COLLATE latin1_general_cs;
