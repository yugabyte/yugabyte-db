-- Copyright (c) YugaByte, Inc.

 -- HA Standby Last Backup
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'HA Standby Sync',
  'Backup sync to standby has failed',
  current_timestamp,
  'PLATFORM',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":900.0}}',
  'SECOND',
  'HA_STANDBY_SYNC',
  true,
  true
from customer;

select create_customer_alert_definitions('HA Standby Sync', false);
