-- Copyright (c) YugaByte, Inc.

select replace_configuration_query(
 'NODE_DOWN',
 'count by (node_prefix) (label_replace(max_over_time('
   || 'up{export_type="node_export",node_prefix="__nodePrefix__"}[15m]), '
   || '"exported_instance", "$1", "instance", "(.*)") < 1 and '
   || 'on (node_prefix, export_type, exported_instance) (min_over_time('
   || 'ybp_universe_node_process_status{node_prefix="__nodePrefix__"}[1m]) == 1)) '
   || '{{ query_condition }} {{ query_threshold }}');

select replace_configuration_query(
 'DB_INSTANCE_DOWN',
 'count by (node_prefix) (label_replace(max_over_time('
   || 'up{export_type=~"master_export|tserver_export",node_prefix="__nodePrefix__"}[15m]), '
   || '"exported_instance", "$1", "instance", "(.*)") < 1 and on (node_prefix, export_type, exported_instance) '
   || '(min_over_time(ybp_universe_node_process_status{node_prefix="__nodePrefix__"}[1m]) == 1)) '
   || '{{ query_condition }} {{ query_threshold }}');
