-- Copyright (c) YugaByte, Inc.

-- SSH_KEY_ROTATION_FAILURE
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'SSH Key Rotation Failure',
  'Last SSH Key Rotation task failed for universe',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'SSH_KEY_ROTATION_FAILURE',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'SSH Key Rotation Failure',
 'last_over_time(ybp_ssh_key_rotation_status{universe_uuid = "__universeUuid__"}[1d])'
    || ' {{ query_condition }} 1');
