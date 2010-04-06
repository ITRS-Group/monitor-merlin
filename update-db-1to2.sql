ALTER TABLE notification
	ADD COLUMN command_name VARCHAR(255) AFTER
	service_description;

UPDATE db_version SET version = 2;
