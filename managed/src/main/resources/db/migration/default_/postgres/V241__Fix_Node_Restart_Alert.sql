-- Copyright (c) YugaByte, Inc.

select replace_configuration_query(
 'NODE_RESTART',
 'max by (node_prefix) '
   || '(changes(node_boot_time_seconds{node_prefix="__nodePrefix__"}[30m])) '
   || '{{ query_condition }} {{ query_threshold }}');
