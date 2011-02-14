RENAME TABLE comment TO comment_tbl;

ALTER TABLE host
	CHANGE check_flapping_recovery_notification check_flapping_recovery_notifi int(10) NOT NULL default '0';
ALTER TABLE service
	CHANGE check_flapping_recovery_notification check_flapping_recovery_notifi int(10) NOT NULL default '0';

UPDATE db_version SET version = 2;
