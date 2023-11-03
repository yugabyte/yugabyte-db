-- Copyright (c) YugaByte, Inc.

-- NODE_OOM_KILLS
select replace_configuration_query(
 'NODE_OOM_KILLS',
 'count by (node_prefix) (yb_node_oom_kills_10min{node_prefix="__nodePrefix__"} '
    || '{{ query_condition }} {{ query_threshold }}) > 0');
