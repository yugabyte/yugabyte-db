-- Copyright (c) YugaByte, Inc.

alter table alert_definition_group drop constraint ck_adg_threshold_unit;
alter table alert_definition_group add constraint ck_adg_threshold_unit check (threshold_unit in ('STATUS','COUNT','PERCENT','MILLISECOND','SECOND','DAY'));

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
    query TEXT;
  BEGIN
    FOR customerRecord IN
      SELECT * FROM customer
    LOOP
      groupUuid :=  (select uuid from alert_definition_group where name = groupName and customer_uuid = customerRecord.uuid);
      FOR universeRecord IN
        SELECT * FROM universe where universe.customer_id = customerRecord.id
      LOOP
        definitionUuid := gen_random_uuid();
        query := replace(queryTemplate, '__universeUuid__', universeRecord.universe_uuid::text);
        query := replace(query, '__nodePrefix__', universeRecord.universe_details_json::jsonb->>'nodePrefix');
        insert into alert_definition
          (uuid, query, customer_uuid, group_uuid)
        values
          (definitionUuid, query, customerRecord.uuid, groupUuid);
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

-- BACKUP_SCHEDULE_FAILURE
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Backup Schedule Failure',
  'Last attempt to run scheduled backup failed due to other backup or universe operation in progress',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'BACKUP_SCHEDULE_FAILURE',
  true
from customer;

select create_universe_alert_definitions(
 'Backup Schedule Failure',
 'ybp_schedule_backup_status{universe_uuid = "__universeUuid__"} {{ query_condition }} 1');

-- NODE_DOWN
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'DB node down',
  'DB node is down for 15 minutes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'NODE_DOWN',
  true
from customer;

select create_universe_alert_definitions(
 'DB node down',
 'count by (node_prefix) (max_over_time(up{export_type="node_export",node_prefix="__nodePrefix__"}[15m]) < 1) '
   || '{{ query_condition }} {{ query_threshold }}');

-- NODE_RESTART
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'DB node restart',
  'Unexpected DB node restart(s) occurred during last 30 minutes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":0}, "SEVERE":{"condition":"GREATER_THAN", "threshold":2}}',
  'COUNT',
  'NODE_RESTART',
  true
from customer;

select create_universe_alert_definitions(
 'DB node restart',
 'max by (node_prefix) (changes(node_boot_time{node_prefix="__nodePrefix__"}[30m])) '
   || '{{ query_condition }} {{ query_threshold }}');

-- NODE_CPU_USAGE
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'DB node CPU usage',
  'Average node CPU usage percentage for 30 minutes is above threshold',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":90}, "SEVERE":{"condition":"GREATER_THAN", "threshold":95}}',
  'PERCENT',
  'NODE_CPU_USAGE',
  true
from customer;

select create_universe_alert_definitions(
 'DB node CPU usage',
 'count by (node_prefix) ((100 - (avg by (node_prefix, instance) (avg_over_time(irate('
   || 'node_cpu{job="node",mode="idle",node_prefix="__nodePrefix__"}[1m])[30m:])) * 100)) '
   || '{{ query_condition }} {{ query_threshold }})');

-- NODE_DISK_USAGE
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'DB node disk usage',
  'Node Disk usage percentage is above threshold',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":70}}',
  'PERCENT',
  'NODE_DISK_USAGE',
  true
from customer;

select create_universe_alert_definitions(
 'DB node disk usage',
 'count by (node_prefix) (100 - (sum without (saved_name) (node_filesystem_free{mountpoint=~"/mnt/.*", node_prefix="__nodePrefix__"}) '
   || '/ sum without (saved_name) (node_filesystem_size{mountpoint=~"/mnt/.*", node_prefix="__nodePrefix__"}) * 100) '
   || '{{ query_condition }} {{ query_threshold }})');

-- NODE_FILE_DESCRIPTORS_USAGE
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'DB node file descriptors usage',
  'Node file descriptors usage percentage is above threshold',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":70}}',
  'PERCENT',
  'NODE_FILE_DESCRIPTORS_USAGE',
  true
from customer;

select create_universe_alert_definitions(
 'DB node file descriptors usage',
 'count by (universe_uuid) (ybp_health_check_used_fd_pct{universe_uuid="__universeUuid__"} * 100 '
   || '{{ query_condition }} {{ query_threshold }})');

-- DB_VERSION_MISMATCH
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, duration_sec, active)
select
  gen_random_uuid(),
  uuid,
  'DB version mismatch',
  'DB Master/TServer version does not match Platform universe version',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_VERSION_MISMATCH',
  3600,
  true
from customer;

select create_universe_alert_definitions(
 'DB version mismatch',
 'ybp_health_check_tserver_version_mismatch{universe_uuid="__universeUuid__"} '
   || '+ ybp_health_check_master_version_mismatch{universe_uuid="__universeUuid__"} '
   || '{{ query_condition }} {{ query_threshold }}');

-- DB_INSTANCE_DOWN
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'DB instance down',
  'DB Master/TServer instance is down for 15 minutes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_INSTANCE_DOWN',
  true
from customer;

select create_universe_alert_definitions(
 'DB instance down',
 'count by (node_prefix) (max_over_time(up{export_type=~"master_export|tserver_export",node_prefix="__nodePrefix__"}[15m]) < 1) '
   || '{{ query_condition }} {{ query_threshold }}');

-- DB_INSTANCE_RESTART
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'DB Instance restart',
  'Unexpected Master or TServer process restart(s) occurred during last 30 minutes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":0}, "SEVERE":{"condition":"GREATER_THAN", "threshold":2}}',
  'COUNT',
  'DB_INSTANCE_RESTART',
  true
from customer;

select create_universe_alert_definitions(
 'DB Instance restart',
 'max by (universe_uuid) (changes(ybp_health_check_master_boot_time_sec{universe_uuid="__universeUuid__"}[30m])) '
   || '{{ query_condition }} {{ query_threshold }}');

-- DB_FATAL_LOGS
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'DB fatal logs',
  'Fatal logs detected on DB Master/TServer instances',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_FATAL_LOGS',
  true
from customer;

select create_universe_alert_definitions(
 'DB fatal logs',
 'ybp_health_check_master_fatal_logs{universe_uuid="__universeUuid__"} '
   || '+ ybp_health_check_tserver_fatal_logs{universe_uuid="__universeUuid__"} '
   || '{{ query_condition }} {{ query_threshold }}');

-- DB_CORE_FILES
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'DB core files',
  'Core files detected on DB TServer instances',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_CORE_FILES',
  true
from customer;

select create_universe_alert_definitions(
 'DB core files',
 'ybp_health_check_tserver_core_files{universe_uuid="__universeUuid__"} '
   || '{{ query_condition }} {{ query_threshold }}');

-- DB_YSQL_CONNECTION
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'DB YSQLSH connection',
  'YSQLSH connection to DB instances failed',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_YSQL_CONNECTION',
  true
from customer;

select create_universe_alert_definitions(
 'DB YSQLSH connection',
 'ybp_health_check_ysqlsh_connectivity_error{universe_uuid="__universeUuid__"} '
   || '{{ query_condition }} {{ query_threshold }}');

-- DB_YCQL_CONNECTION
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'DB CQLSH connection',
  'CQLSH connection to DB instances failed',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_YCQL_CONNECTION',
  true
from customer;

select create_universe_alert_definitions(
 'DB CQLSH connection',
 'ybp_health_check_cqlsh_connectivity_error{universe_uuid="__universeUuid__"} '
   || '{{ query_condition }} {{ query_threshold }}');

-- DB_REDIS_CONNECTION
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'DB Redis connection',
  'Redis connection to DB instances failed',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_REDIS_CONNECTION',
  true
from customer;

select create_universe_alert_definitions(
 'DB Redis connection',
 'ybp_health_check_redis_connectivity_error{universe_uuid="__universeUuid__"} '
   || '{{ query_condition }} {{ query_threshold }}');

-- NODE_TO_NODE_CA_CERT_EXPIRY
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Node to node CA cert expiry',
  'Node to node CA certificate expires soon',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":30}}',
  'DAY',
  'NODE_TO_NODE_CA_CERT_EXPIRY',
  true
from customer;

select create_universe_alert_definitions(
 'Node to node CA cert expiry',
 'count by (universe_uuid) (ybp_health_check_n2n_ca_cert_validity_days{universe_uuid="__universeUuid__"} '
   || '{{ query_condition }} {{ query_threshold }})');

-- NODE_TO_NODE_CERT_EXPIRY
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Node to node cert expiry',
  'Node to node certificate expires soon',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":30}}',
  'DAY',
  'NODE_TO_NODE_CERT_EXPIRY',
  true
from customer;

select create_universe_alert_definitions(
 'Node to node cert expiry',
 'count by (universe_uuid) (ybp_health_check_n2n_cert_validity_days{universe_uuid="__universeUuid__"} '
   || '{{ query_condition }} {{ query_threshold }})');

-- CLIENT_TO_NODE_CA_CERT_EXPIRY
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Client to node CA cert expiry',
  'Client to node CA certificate expires soon',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":30}}',
  'DAY',
  'CLIENT_TO_NODE_CA_CERT_EXPIRY',
  true
from customer;

select create_universe_alert_definitions(
 'Client to node CA cert expiry',
 'count by (universe_uuid) (ybp_health_check_c2n_ca_cert_validity_days{universe_uuid="__universeUuid__"} '
   || '{{ query_condition }} {{ query_threshold }})');

-- CLIENT_TO_NODE_CERT_EXPIRY
insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active)
select
  gen_random_uuid(),
  uuid,
  'Client to node cert expiry',
  'Client to node certificate expires soon',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":30}}',
  'DAY',
  'CLIENT_TO_NODE_CERT_EXPIRY',
  true
from customer;

select create_universe_alert_definitions(
 'Client to node cert expiry',
 'count by (universe_uuid) (ybp_health_check_c2n_cert_validity_days{universe_uuid="__universeUuid__"} '
   || '{{ query_condition }} {{ query_threshold }})');

-- Implemented separate alerts for each health check issue
delete from alert_definition_group where template = 'HEALTH_CHECK_NODE_ERRORS';
update alert_definition set config_written = false;

update alert set group_type = 'UNIVERSE' where group_type is null and name in ('Replication Lag', 'Clock Skew', 'Memory Consumption');
update alert set group_type = 'CUSTOMER' where group_type is null;
alter table alert alter column group_type set not null;
