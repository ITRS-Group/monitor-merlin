DROP SEQUENCE report_data_id_SEQ;
DROP TRIGGER report_data_id_TRG;
ALTER TABLE report_data DROP COLUMN id;
CREATE SEQUENCE rename_log_id_SEQ MINVALUE 1 MAXVALUE 999999999999999999999999 INCREMENT BY 1  NOCYCLE;
ALTER TABLE notification MODIFY command_name VARCHAR2(1024 CHAR);

CREATE TABLE rename_log (
  id NUMBER(10,0) NOT NULL,
  from_host_name VARCHAR2(255 CHAR),
  from_service_description VARCHAR2(255 CHAR) DEFAULT NULL,
  to_host_name VARCHAR2(255 CHAR),
  to_service_description VARCHAR2(255 CHAR) DEFAULT NULL
);

ALTER TABLE rename_log ADD CONSTRAINT rename_log_pk PRIMARY KEY (id) ENABLE;


CREATE OR REPLACE TRIGGER rename_log_id_TRG BEFORE INSERT OR UPDATE ON rename_log
FOR EACH ROW
DECLARE
v_newVal NUMBER(12) := 0;
v_incVal NUMBER(12) := 0;
BEGIN
  IF INSERTING AND :new.id IS NULL THEN
    SELECT  rename_log_id_SEQ.NEXTVAL INTO v_newVal FROM DUAL;
    IF v_newVal = 1 THEN
      SELECT NVL(max(id),0) INTO v_newVal FROM rename_log;
      v_newVal := v_newVal + 1;
      LOOP
        EXIT WHEN v_incval>=v_newVal;
        SELECT rename_log_id_SEQ.nextval INTO v_incval FROM dual;
      END LOOP;
    END IF;
    :new.id := v_newVal;
  END IF;
END;

/
