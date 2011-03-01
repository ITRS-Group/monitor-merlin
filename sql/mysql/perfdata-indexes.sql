CREATE INDEX pd_time ON perfdata(timestamp);
CREATE INDEX pd_host_name ON perfdata(host_name);
CREATE INDEX pd_service_name ON perfdata(host_name, service_description);
