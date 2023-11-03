-- Copyright (c) YugaByte, Inc.

-- DB_MEMORY_OVERLOAD
select replace_configuration_query(
 'DB_MEMORY_OVERLOAD',
 'sum by (node_prefix) (increase('
   || 'leader_memory_pressure_rejections{node_prefix="__nodePrefix__"}[10m])) + '
   || 'sum by (node_prefix) (increase('
   || 'follower_memory_pressure_rejections{node_prefix="__nodePrefix__"}[10m])) + '
   || 'sum by (node_prefix) (increase('
   || 'operation_memory_pressure_rejections{node_prefix="__nodePrefix__"}[10m])) '
   || '{{ query_condition }} {{ query_threshold }}');

-- DB_COMPACTION_OVERLOAD
select replace_configuration_query(
 'DB_COMPACTION_OVERLOAD',
 'sum by (node_prefix) (increase('
   || 'majority_sst_files_rejections{node_prefix="__nodePrefix__"}[10m])) '
   || '{{ query_condition }} {{ query_threshold }}');

-- DB_QUEUES_OVERFLOW
select replace_configuration_query(
 'DB_QUEUES_OVERFLOW',
 'sum by (node_prefix) (increase('
   || 'rpcs_queue_overflow{node_prefix="__nodePrefix__"}[10m])) + '
   || 'sum by (node_prefix) (increase('
   || 'rpcs_timed_out_in_queue{node_prefix="__nodePrefix__"}[10m])) '
   || '{{ query_condition }} {{ query_threshold }}');
