-- Copyright (c) YugaByte, Inc.

-- DB_MEMORY_OVERLOAD
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DB memory overload',
  'DB memory rejections detected during last 10 minutes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_MEMORY_OVERLOAD',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'DB memory overload',
 'sum by (node_prefix) (sum_over_time('
   || 'leader_memory_pressure_rejections{node_prefix="__nodePrefix__"}[10m])) + '
   || 'sum by (node_prefix) (sum_over_time('
   || 'follower_memory_pressure_rejections{node_prefix="__nodePrefix__"}[10m])) + '
   || 'sum by (node_prefix) (sum_over_time('
   || 'operation_memory_pressure_rejections{node_prefix="__nodePrefix__"}[10m])) '
   || '{{ query_condition }} {{ query_threshold }}');

-- DB_COMPACTION_OVERLOAD
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DB compaction overload',
  'DB compaction rejections detected during last 10 minutes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_COMPACTION_OVERLOAD',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'DB compaction overload',
 'sum by (node_prefix) (sum_over_time('
   || 'majority_sst_files_rejections{node_prefix="__nodePrefix__"}[10m])) '
   || '{{ query_condition }} {{ query_threshold }}');

-- DB_QUEUES_OVERFLOW
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DB queues overflow',
  'DB queues overflow detected during last 10 minutes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_QUEUES_OVERFLOW',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'DB queues overflow',
 'sum by (node_prefix) (sum_over_time('
   || 'rpcs_queue_overflow{node_prefix="__nodePrefix__"}[10m])) + '
   || 'sum by (node_prefix) (sum_over_time('
   || 'rpcs_timed_out_in_queue{node_prefix="__nodePrefix__"}[10m])) '
   || '{{ query_condition }} {{ query_threshold }}');
