-- Copyright (c) YugaByte, Inc.

 -- THP settings alert
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Incorrect THP settings',
  'Found incorrect THP settings on universe nodes.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'THP_INCORRECT_SETTINGS',
  true,
  true
from customer;

select create_universe_alert_definitions('Incorrect THP settings');