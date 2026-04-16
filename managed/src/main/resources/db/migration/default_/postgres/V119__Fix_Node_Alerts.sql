-- Copyright (c) YugaByte, Inc.

select replace_configuration_query(
 'MEMORY_CONSUMPTION',
 '(max by (node_prefix) (avg_over_time(node_memory_MemTotal_bytes{node_prefix="__nodePrefix__"}[10m])) - '
   || 'max by (node_prefix) (avg_over_time(node_memory_Buffers_bytes{node_prefix="__nodePrefix__"}[10m])) - '
   || 'max by (node_prefix) (avg_over_time(node_memory_Cached_bytes{node_prefix="__nodePrefix__"}[10m])) - '
   || 'max by (node_prefix) (avg_over_time(node_memory_MemFree_bytes{node_prefix="__nodePrefix__"}[10m])) - '
   || 'max by (node_prefix) (avg_over_time(node_memory_Slab_bytes{node_prefix="__nodePrefix__"}[10m]))) / '
   || '(max by (node_prefix) (avg_over_time(node_memory_MemTotal_bytes{node_prefix="__nodePrefix__"}[10m]))) * 100 '
   || '{{ query_condition }} {{ query_threshold }}');

select replace_configuration_query(
 'NODE_CPU_USAGE',
 'count by(node_prefix) ((100 - (avg by (node_prefix, instance) '
   || '(avg_over_time(irate(node_cpu_seconds_total{job="node",mode="idle", '
   || 'node_prefix="__nodePrefix__"}[1m])[30m:])) * 100)) '
   || '{{ query_condition }} {{ query_threshold }})');

select replace_configuration_query(
 'NODE_DISK_USAGE',
 'count by (node_prefix) (100 - (sum without (saved_name) '
   || '(node_filesystem_free_bytes{mountpoint=~"/mnt/.*", node_prefix="__nodePrefix__"}) / sum without (saved_name) '
   || '(node_filesystem_size_bytes{mountpoint=~"/mnt/.*", node_prefix="__nodePrefix__"}) * 100) '
   || '{{ query_condition }} {{ query_threshold }})');
