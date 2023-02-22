-- Copyright (c) YugaByte, Inc.

-- DB_INSTANCE_RESTART
select replace_configuration_query(
 'DB_INSTANCE_RESTART',
 'max by (node_prefix) (changes(yb_node_boot_time{node_prefix="__nodePrefix__"}[30m]) and on '
   || '(node_prefix) (max_over_time(ybp_universe_update_in_progress'
   || '{node_prefix="__nodePrefix__"}[31m]) == 0)) '
   || '{{ query_condition }} {{ query_threshold }}');
