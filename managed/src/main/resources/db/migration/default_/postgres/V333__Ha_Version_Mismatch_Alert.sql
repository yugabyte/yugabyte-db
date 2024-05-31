-- Copyright (c) YugaByte, Inc.

 -- HA Standby Last Backup
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'HA Version Mismatch',
  'Mismatch between active and standby',
  current_timestamp,
  'PLATFORM',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'STATUS',
  'HA_VERSION_MISMATCH',
  true,
  true
from customer;

select create_customer_alert_definitions('HA Version Mismatch', false);