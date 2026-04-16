-- Copyright (c) YugaByte, Inc.

alter table alert_definition_group drop constraint ck_adg_threshold_unit;
alter table alert_definition_group add constraint ck_adg_threshold_unit check (threshold_unit in ('STATUS','COUNT','PERCENT','MILLISECOND'));

CREATE OR REPLACE FUNCTION create_universe_alert_definitions(groupName text, queryTemplate text)
 RETURNS boolean
 language plpgsql
 as
$$
  DECLARE
    customerRecord RECORD;
    universeRecord RECORD;
    groupUuid UUID;
    definitionUuid UUID;
  BEGIN
    FOR customerRecord IN
      SELECT * FROM customer
    LOOP
      groupUuid :=  (select uuid from alert_definition_group where name = groupName and customer_uuid = customerRecord.uuid);
      FOR universeRecord IN
        SELECT * FROM universe where universe.customer_id = customerRecord.id
      LOOP
        definitionUuid := gen_random_uuid();
        insert into alert_definition
          (uuid, query, customer_uuid, group_uuid)
        values
          (definitionUuid, replace(queryTemplate, '__universeUuid__', universeRecord.universe_uuid::text), customerRecord.uuid, groupUuid);
        insert into alert_definition_label
          (definition_uuid, name, value)
        values
          (definitionUuid, 'target_type', 'universe'),
          (definitionUuid, 'target_name', universeRecord.name),
          (definitionUuid, 'target_uuid', universeRecord.universe_uuid),
          (definitionUuid, 'universe_name', universeRecord.name),
          (definitionUuid, 'universe_uuid', universeRecord.universe_uuid);
      END LOOP;
    END LOOP;
    RETURN true;
  END;
$$;

CREATE OR REPLACE FUNCTION create_customer_alert_definitions(groupName text, queryTemplate text, skipTargetLabels boolean)
 RETURNS boolean
 language plpgsql
 as
$$
  DECLARE
    customerRecord RECORD;
    groupUuid UUID;
    definitionUuid UUID;
  BEGIN
    FOR customerRecord IN
      SELECT * FROM customer
    LOOP
      groupUuid :=  (select uuid from alert_definition_group where name = groupName and customer_uuid = customerRecord.uuid);
      definitionUuid := gen_random_uuid();
      insert into alert_definition
        (uuid, query, customer_uuid, group_uuid)
      values
        (definitionUuid, replace(queryTemplate, '__customerUuid__', customerRecord.uuid::text), customerRecord.uuid, groupUuid);
      IF NOT skipTargetLabels THEN
        insert into alert_definition_label
          (definition_uuid, name, value)
        values
          (definitionUuid, 'target_type', 'customer'),
          (definitionUuid, 'target_name', customerRecord.code),
          (definitionUuid, 'target_uuid', customerRecord.uuid);
      END IF;
    END LOOP;
    RETURN true;
  END;
$$;

-- HEALTH_CHECK_ERROR
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Health Check Error',
  'Failed to perform health check',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'HEALTH_CHECK_ERROR',
  true
from customer;

select create_universe_alert_definitions(
 'Health Check Error',
 'ybp_health_check_status{universe_uuid = "__universeUuid__"} {{ query_condition }} 1');

-- HEALTH_CHECK_NODE_ERRORS
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Health Check Node Errors',
  'Number of nodes with health check errors is above threshold',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'HEALTH_CHECK_NODE_ERRORS',
  true
from customer;

select create_universe_alert_definitions(
 'Health Check Node Errors',
 'ybp_health_check_nodes_with_errors{universe_uuid = "__universeUuid__"} {{ query_condition }} {{ query_threshold }}');

-- HEALTH_CHECK_NOTIFICATION_ERROR
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Health Check Notification Error',
  'Failed to perform health check notification',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'HEALTH_CHECK_NOTIFICATION_ERROR',
  true
from customer;

select create_universe_alert_definitions(
 'Health Check Notification Error',
 'ybp_health_check_notification_status{universe_uuid = "__universeUuid__"} {{ query_condition }} 1');

-- BACKUP_FAILURE
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Backup Failure',
  'Last universe backup creation task failed',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'BACKUP_FAILURE',
  true
from customer;

select create_universe_alert_definitions(
 'Backup Failure',
 'ybp_create_backup_status{universe_uuid = "__universeUuid__"} {{ query_condition }} 1');

update alert_definition_group set active = false
 where name = 'Backup Failure' and customer_uuid in
  (select customer_uuid from customer_config
     where type = 'ALERTS' and (data ->> 'reportBackupFailures')::boolean = false);

-- INACTIVE_CRON_NODES
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Inactive Cronjob Nodes',
  'Number of nodes with inactive cronjob is above threshold',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'INACTIVE_CRON_NODES',
  true
from customer;

select create_universe_alert_definitions(
 'Inactive Cronjob Nodes',
 'ybp_universe_inactive_cron_nodes{universe_uuid = "__universeUuid__"} {{ query_condition }} {{ query_threshold }}');

-- ALERT_QUERY_FAILED
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Alert Query Failed',
  'Failed to query alerts from Prometheus',
  current_timestamp,
  'CUSTOMER',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'ALERT_QUERY_FAILED',
  true
from customer;

select create_customer_alert_definitions(
 'Alert Query Failed',
 'ybp_alert_query_status {{ query_condition }} 1',
 false);

-- ALERT_CONFIG_WRITING_FAILED
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Alert Rules Sync Failed',
  'Failed to sync alerting rules to Prometheus',
  current_timestamp,
  'CUSTOMER',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'ALERT_CONFIG_WRITING_FAILED',
  true
from customer;

select create_customer_alert_definitions(
 'Alert Rules Sync Failed',
 'ybp_alert_config_writer_status {{ query_condition }} 1',
 false);

-- ALERT_NOTIFICATION_ERROR
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Alert Notification Failed',
  'Failed to send alert notifications',
  current_timestamp,
  'CUSTOMER',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'ALERT_NOTIFICATION_ERROR',
  true
from customer;

select create_customer_alert_definitions(
 'Alert Notification Failed',
 'ybp_alert_manager_status{customer_uuid = "__customerUuid__"} {{ query_condition }} 1',
 false);

-- ALERT_NOTIFICATION_CHANNEL_ERROR
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Alert Channel Failed',
  'Failed to send alerts to notification channel',
  current_timestamp,
  'CUSTOMER',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'ALERT_NOTIFICATION_CHANNEL_ERROR',
  true
from customer;

select create_customer_alert_definitions(
 'Alert Channel Failed',
 'ybp_alert_manager_receiver_status{customer_uuid = "__customerUuid__"} {{ query_condition }} 1',
 true);

update alert_definition set config_written = false;