-- Copyright (c) YugaByte, Inc.

-- ENCRYPTION_AT_REST_CONFIG_EXPIRY
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Encryption At Rest config expiry',
  'Encryption At Rest config expires soon',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":3}}',
  'STATUS',
  'ENCRYPTION_AT_REST_CONFIG_EXPIRY',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'Encryption At Rest config expiry',
 'ybp_universe_encryption_key_expiry_days{universe_uuid="__universeUuid__"}'
   || ' {{ query_condition }} {{ query_threshold }}');
