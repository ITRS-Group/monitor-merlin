CREATE TABLE report_data_tmp (
	`id` bigint NOT NULL AUTO_INCREMENT PRIMARY KEY,
	`timestamp` int(11) NOT NULL DEFAULT '0',
	`event_type` int(11) NOT NULL DEFAULT '0',
	`flags` int(11) DEFAULT NULL,
	`attrib` int(11) DEFAULT NULL,
	`host_name` varchar(255) COLLATE latin1_general_cs DEFAULT '',
	`service_description` varchar(255) COLLATE latin1_general_cs DEFAULT '',
	`state` int(2) NOT NULL DEFAULT '0',
	`hard` int(2) NOT NULL DEFAULT '0',
	`retry` int(5) NOT NULL DEFAULT '0',
	`downtime_depth` int(11) DEFAULT NULL,
	`output` text COLLATE latin1_general_cs,
	`long_output` text COLLATE latin1_general_cs,
	KEY `rd_timestamp` (`timestamp`),
	KEY `rd_event_type` (`event_type`),
	KEY `rd_name_evt_time` (`host_name`,`service_description`,`event_type`,`hard`,`timestamp`),
	KEY `rd_state` (`state`)
);

INSERT INTO report_data_tmp (`timestamp`, `event_type`, `flags`, `attrib`, `host_name`, `service_description`, `state`, `hard`, `retry`, `downtime_depth`, `output`)
SELECT `timestamp`, `event_type`, `flags`, `attrib`, `host_name`, `service_description`, `state`, `hard`, `retry`, `downtime_depth`, `output`
FROM report_data;

ALTER TABLE report_data RENAME report_data_old;
ALTER TABLE report_data_tmp RENAME report_data;
DROP TABLE report_data_old;
