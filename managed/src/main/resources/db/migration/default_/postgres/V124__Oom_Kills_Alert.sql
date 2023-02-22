-- Copyright (c) YugaByte, Inc.

-- NODE_OOM_KILLS
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DB node OOM',
  'Number of OOM kills during last 10 minutes is above threshold',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":1},'
    || '"SEVERE":{"condition":"GREATER_THAN", "threshold":3}}',
  'COUNT',
  'NODE_OOM_KILLS',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'DB node OOM',
 'count by (node_prefix) (yb_node_oom_kills{node_prefix="__nodePrefix__"}[10m] '
   || '{{ query_condition }} {{ query_threshold }}) > 0');
