-- Copyright (c) YugaByte, Inc.

-- DB_WRITE_READ_TEST_ERROR
select replace_configuration_query(
 'DB_WRITE_READ_TEST_ERROR',
 'count by (node_prefix) ((yb_node_ysql_write_read{node_prefix="__nodePrefix__"} and on'
   || ' (node_prefix) (max_over_time(ybp_universe_update_in_progress'
   || '{node_prefix="__nodePrefix__"}[5m]) == 0)) {{ query_condition }} {{ query_threshold }})');
