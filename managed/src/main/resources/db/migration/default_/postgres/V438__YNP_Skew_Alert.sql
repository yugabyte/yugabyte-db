-- Copyright (c) YugaByte, Inc.

-- YNP version skew alert
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'YNP Version Skew',
  'YNP version on node is behind the YBA YNP version.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'YNP_VERSION_SKEW',
  true,
  true
from customer;

select create_universe_alert_definitions('YNP Version Skew');