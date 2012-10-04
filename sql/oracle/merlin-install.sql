DROP SEQUENCE notification_id_SEQ;
CREATE SEQUENCE  notification_id_SEQ
  MINVALUE 1 MAXVALUE 999999999999999999999999 INCREMENT BY 1  NOCYCLE ;

CREATE TABLE merlin_importer
(
  pid NUMBER(10,0) DEFAULT NULL
);

CREATE TABLE notification (
  instance_id NUMBER(10,0) DEFAULT '0' NOT NULL,
  id NUMBER(10,0) NOT NULL,
  notification_type NUMBER(10,0),
  start_time NUMBER(10,0),
  end_time NUMBER(10,0),
  contact_name VARCHAR2(255 CHAR),
  host_name VARCHAR2(255 CHAR),
  service_description VARCHAR2(255 CHAR),
  command_name VARCHAR2(1024 CHAR),
  reason_type NUMBER(10,0),
  state NUMBER(10,0),
  output CLOB,
  ack_author VARCHAR2(255 CHAR),
  ack_data CLOB,
  escalated NUMBER(10,0),
  contacts_notified NUMBER(10,0)
);
ALTER TABLE notification ADD CONSTRAINT PRIMARY_8 PRIMARY KEY(id) ENABLE;
CREATE INDEX n_host_name ON notification(host_name);
CREATE INDEX n_service_name ON notification(host_name, service_description);
CREATE INDEX n_contact_name ON notification(contact_name);


CREATE TABLE perfdata (
  timestamp NUMBER(10,0) NOT NULL,
  host_name VARCHAR2(255 CHAR) NOT NULL,
  service_description VARCHAR2(255 CHAR),
  perfdata CLOB NOT NULL
);
CREATE INDEX pd_time ON perfdata(timestamp);
CREATE INDEX pd_host_name ON perfdata(host_name);
CREATE INDEX pd_service_name ON perfdata(host_name, service_description);

CREATE TABLE report_data (
  timestamp NUMBER(10,0) DEFAULT '0' NOT NULL,
  event_type NUMBER(10,0) DEFAULT '0' NOT NULL,
  flags NUMBER(10,0),
  attrib NUMBER(10,0),
  host_name VARCHAR2(255 CHAR),
  service_description VARCHAR2(255 CHAR),
  state NUMBER(10,0) DEFAULT '0' NOT NULL,
  hard NUMBER(10,0) DEFAULT '0' NOT NULL,
  retry NUMBER(10,0) DEFAULT '0' NOT NULL,
  downtime_depth NUMBER(10,0),
  output CLOB,
  id NUMBER(10,0)
);
CREATE INDEX rd_timestamp ON report_data(timestamp);
CREATE INDEX rd_event_type ON report_data(event_type);
CREATE INDEX rd_host_name ON report_data(host_name);
CREATE INDEX rd_service_name ON report_data(host_name, service_description);
CREATE INDEX rd_state ON report_data(state);

CREATE OR REPLACE TRIGGER notification_id_TRG BEFORE INSERT OR UPDATE ON notification
FOR EACH ROW
DECLARE
v_newVal NUMBER(12) := 0;
v_incval NUMBER(12) := 0;
BEGIN
  IF INSERTING AND :new.id IS NULL THEN
    SELECT  notification_id_SEQ.NEXTVAL INTO v_newVal FROM DUAL;
    -- If this is the first time this table have been inserted into (sequence == 1)
    IF v_newVal = 1 THEN
      --get the max indentity value from the table
      SELECT NVL(max(id),0) INTO v_newVal FROM notification;
      v_newVal := v_newVal + 1;
      --set the sequence to that value
      LOOP
           EXIT WHEN v_incval>=v_newVal;
           SELECT notification_id_SEQ.nextval INTO v_incval FROM dual;
      END LOOP;
    END IF;
    --used to emulate LAST_INSERT_ID()
    --mysql_utilities.identity := v_newVal;
   -- assign the value from the sequence to emulate the identity column
   :new.id := v_newVal;
  END IF;
END;

/
