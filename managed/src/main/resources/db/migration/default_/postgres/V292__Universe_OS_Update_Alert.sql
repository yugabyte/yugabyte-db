-- Copyright (c) YugaByte, Inc.

-- Universe OS Update Required.
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Universe OS outdated',
  'More recent OS version is recommended for this universe.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":0}}',
  'STATUS',
  'UNIVERSE_OS_UPDATE_REQUIRED',
  true,
  true
from customer;

select create_universe_alert_definitions('Universe OS outdated');