-- Copyright (c) YugaByte, Inc.

-- DB_WRITE_READ_TEST_ERROR
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DB write/read test error',
  'Failed to perform test write/read YSQL operation',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'DB_WRITE_READ_TEST_ERROR',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'DB write/read test error',
 'count by (node_prefix) (yb_node_ysql_write_read{node_prefix="__nodePrefix__"}'
   || ' {{ query_condition }} {{ query_threshold }})');
