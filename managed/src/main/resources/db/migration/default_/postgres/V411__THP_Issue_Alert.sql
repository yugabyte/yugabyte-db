-- Copyright (c) YugaByte, Inc.

-- THP RSS issue
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'THP Issue Threshold Reached',
  'Node memory is high AND RSS exceeds 25% of total memory allocated on a universe',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0.25}}',
  'COUNT',
  'THP_RSS_ISSUE',
  true,
  true
from customer;

select create_universe_alert_definitions('THP Issue Threshold Reached');
