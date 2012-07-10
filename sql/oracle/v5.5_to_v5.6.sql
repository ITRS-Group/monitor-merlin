DROP INDEX rd_host_name ON report_data;
CREATE INDEX rd_evt_service_name ON report_data(event_type, host_name, service_description);
ALTER TABLE program_status ADD (
  peer_id NUMBER(10,0),
  node_type NUMBER(6,0),
  config_hash VARCHAR2(255 CHAR),
  self_assigned_peer_id NUMBER(10,0),
  active_peers NUMBER(10,0),
  configured_peers NUMBER(10,0),
  active_pollers NUMBER(10,0),
  configured_pollers NUMBER(10,0),
  active_masters NUMBER(10,0),
  configured_masters NUMBER(10,0),
  host_checks_handled NUMBER(10,0),
  service_checks_handled NUMBER(10,0)
);
ALTER TABLE report_data DROP COLUMN id;
