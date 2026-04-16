-- Copyright (c) YugaByte, Inc.

 -- Clock skew alert
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'High clock drift',
  'Local clock on the node has drift too far from the actual time.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN","threshold":200.0},"SEVERE":{"condition":"GREATER_THAN", "threshold":400.0}}',
  'MILLISECOND',
  'NODE_CLOCK_DRIFT',
  true,
  true
from customer;

select create_universe_alert_definitions('High clock drift');