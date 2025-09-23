-- Copyright (c) YugaByte, Inc.

 -- Node Agent Down
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Node Agent Server Down',
  'Node Agent server has been down for more than 1 minute',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'NODE_AGENT_DOWN',
  true,
  true
from customer;

select create_universe_alert_definitions('Node Agent Server Down');
