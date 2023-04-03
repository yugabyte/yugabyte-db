-- Copyright (c) YugaByte, Inc.

-- SSH_KEY_EXPIRY
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'SSH Key Expiry',
  'SSH Key expires soon',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":30}}',
  'DAY',
  'SSH_KEY_EXPIRY',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'SSH Key Expiry',
 'ybp_universe_ssh_key_expiry_day{universe_uuid="__universeUuid__"}'
   || ' {{ query_condition }} {{ query_threshold }}');
