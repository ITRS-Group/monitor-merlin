CREATE INDEX rd_timestamp ON report_data(timestamp);
CREATE INDEX rd_event_type ON report_data(event_type);
CREATE INDEX rd_service_name ON report_data(host_name, service_description);
CREATE INDEX rd_evt_service_name ON report_data(event_type, host_name, service_description);
CREATE INDEX rd_state ON report_data(state);
