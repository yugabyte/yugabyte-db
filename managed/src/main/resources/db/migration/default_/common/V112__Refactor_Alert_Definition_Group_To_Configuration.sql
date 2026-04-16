-- Copyright (c) YugaByte, Inc.

ALTER TABLE alert_definition_group DROP CONSTRAINT IF EXISTS ck_adg_target_type;
UPDATE alert_definition_group set target_type = 'PLATFORM' where target_type = 'CUSTOMER';
ALTER TABLE alert_definition_group ADD CONSTRAINT ck_ac_target_type
  CHECK (target_type IN ('PLATFORM','UNIVERSE'));
ALTER TABLE alert_definition_group RENAME COLUMN route_uuid TO destination_uuid;
ALTER TABLE alert_definition_group RENAME TO alert_configuration;
ALTER TABLE alert_configuration RENAME CONSTRAINT ck_adg_threshold_unit TO ck_ac_threshold_unit;
ALTER TABLE alert_configuration RENAME CONSTRAINT fk_adg_customer_uuid TO fk_ac_customer_uuid;
ALTER TABLE alert_configuration RENAME CONSTRAINT fk_adg_route_uuid TO fk_ac_route_uuid;
ALTER INDEX IF EXISTS alert_definition_group_pkey RENAME TO alert_configuration_pkey;

ALTER TABLE alert_definition RENAME COLUMN group_uuid TO configuration_uuid;
ALTER TABLE alert RENAME COLUMN group_uuid TO configuration_uuid;
ALTER TABLE alert DROP CONSTRAINT IF EXISTS ck_alert_group_type;
UPDATE alert set group_type = 'PLATFORM' where group_type = 'CUSTOMER';
ALTER TABLE alert ADD CONSTRAINT ck_alert_config_type
  CHECK (group_type IN ('PLATFORM','UNIVERSE'));
ALTER TABLE alert RENAME COLUMN group_type TO configuration_type;
ALTER TABLE alert RENAME COLUMN target_name TO source_name;
ALTER TABLE alert RENAME CONSTRAINT fk_alert_group_uuid TO fk_alert_config_uuid;
ALTER INDEX IF EXISTS ix_alert_customer_group_type RENAME TO ix_alert_customer_config_type;

ALTER TABLE metric RENAME COLUMN target_uuid TO source_uuid;
ALTER TABLE metric RENAME COLUMN target_labels TO source_labels;
ALTER TABLE metric_label RENAME COLUMN target_label TO source_label;

UPDATE alert_label set value = 'PLATFORM' where name = 'group_type' and value = 'CUSTOMER';
UPDATE alert_label set name = 'configuration_type' where name = 'group_type';
UPDATE alert_label set name = 'configuration_uuid' where name = 'group_uuid';
UPDATE alert_label set name = 'source_name' where name = 'target_name';
UPDATE alert_label set value = 'alert channel'
  WHERE name = 'target_type' and value = 'alert receiver';
UPDATE alert_label set name = 'source_type' where name = 'target_type';
UPDATE alert_label set name = 'source_uuid' where name = 'target_uuid';
UPDATE alert_definition_label set name = 'source_name' where name = 'target_name';
UPDATE alert_definition_label set value = 'alert channel'
  WHERE name = 'target_type' and value = 'alert receiver';
UPDATE alert_definition_label set name = 'source_type' where name = 'target_type';
UPDATE alert_definition_label set name = 'source_uuid' where name = 'target_uuid';
UPDATE metric_label set name = 'source_name' where name = 'target_name';
UPDATE metric_label set value = 'alert channel'
  WHERE name = 'target_type' and value = 'alert receiver';
UPDATE metric_label set name = 'source_type' where name = 'target_type';
UPDATE metric_label set name = 'source_uuid' where name = 'target_uuid';
UPDATE alert_definition set config_written = false;


