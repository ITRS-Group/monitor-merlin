CREATE INDEX rd_timestamp ON report_data(timestamp);
CREATE INDEX rd_event_type ON report_data(event_type);
CREATE INDEX rd_name_evt_time ON report_data(host_name,service_description,event_type,hard,timestamp);
CREATE INDEX rd_state ON report_data(state);
