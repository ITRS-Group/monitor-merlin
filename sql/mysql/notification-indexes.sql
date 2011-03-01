CREATE INDEX n_host_name ON notification(host_name);
CREATE INDEX n_service_name ON notification(host_name, service_description);
CREATE INDEX n_contact_name ON notification(contact_name);
