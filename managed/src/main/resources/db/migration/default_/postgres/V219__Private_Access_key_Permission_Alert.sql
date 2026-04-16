-- Copyright (c) YugaByte, Inc.

-- PRIVATE_ACCESS_KEY_STATUS
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Private access key permission status',
  'Change in universe private access keys file permissions',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'PRIVATE_ACCESS_KEY_STATUS',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'Private access key permission status',
 'last_over_time(ybp_universe_private_access_key_status{universe_uuid = "__universeUuid__"}[1d])'
    || ' {{ query_condition }} 1');
