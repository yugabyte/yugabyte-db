-- Copyright (c) YugaByte, Inc.

select replace_configuration_query(
 'NODE_DISK_USAGE',
 'count by (node_prefix) (100 - (sum without (saved_name) '
   || '(node_filesystem_free_bytes{mountpoint=~"/mnt/d[0-9]+", node_prefix="__nodePrefix__"}) / sum without (saved_name) '
   || '(node_filesystem_size_bytes{mountpoint=~"/mnt/d[0-9]+", node_prefix="__nodePrefix__"}) * 100) '
   || '{{ query_condition }} {{ query_threshold }})');
