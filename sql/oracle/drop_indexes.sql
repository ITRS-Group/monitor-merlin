connect merlin/merlin;

DROP INDEX rd_timestamp;
DROP INDEX rd_event_type;
DROP INDEX rd_host_name;
DROP INDEX rd_service_name;
DROP INDEX rd_state;
DROP INDEX pd_time;
DROP INDEX pd_host_name;
DROP INDEX pd_service_name;
DROP INDEX n_host_name;
DROP INDEX n_service_name;
DROP INDEX n_contact_name;

disconnect;
