-- Copyright (c) YugaByte, Inc.

-- LEADERLESS_TABLETS
select replace_configuration_query(
 'LEADERLESS_TABLETS',
 'max by (node_prefix) (count by (node_prefix, exported_instance)'
    || ' (max_over_time(yb_node_leaderless_tablet{node_prefix="__nodePrefix__"}[5m]))'
    || ' {{ query_condition }} {{ query_threshold }})');

-- UNDER_REPLICATED_TABLETS
select replace_configuration_query(
 'UNDER_REPLICATED_TABLETS',
 'max by (node_prefix) (count by (node_prefix, exported_instance)'
    || ' (max_over_time(yb_node_underreplicated_tablet{node_prefix="__nodePrefix__"}[5m]))'
    || ' {{ query_condition }} {{ query_threshold }})');
