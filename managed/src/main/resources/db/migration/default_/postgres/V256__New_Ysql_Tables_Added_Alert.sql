-- Copyright (c) YugaByte, Inc.

-- NEW_YSQL_TABLES_ADDED alert
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'New Ysql Tables Added',
  'New ysql tables added to source universe in db associated with existing xcluster replication config but not added to replication',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"NOT_EQUAL","threshold":0.0}}',
  'COUNT',
  'NEW_YSQL_TABLES_ADDED',
  120,
  true,
  true
from customer;

select create_universe_alert_definitions(
  'New Ysql Tables Added',
  '((count by (namespace_name, node_prefix)'
    || '(count by(namespace_name, table_id, node_prefix)'
    || '(rocksdb_current_version_sst_files_size{node_prefix="__nodePrefix__",'
    || 'table_type="PGSQL_TABLE_TYPE"})))'
    || '- count by(namespace_name, node_prefix)'
    || '(count by(namespace_name, node_prefix, table_id)'
    || '(async_replication_sent_lag_micros{node_prefix="__nodePrefix__",'
    || 'table_type="PGSQL_TABLE_TYPE"})))'
    || ' {{ query_condition }} {{ query_threshold }}');
