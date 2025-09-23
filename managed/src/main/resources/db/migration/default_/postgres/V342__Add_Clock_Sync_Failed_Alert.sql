-- Copyright (c) YugaByte, Inc.

-- Clock sync check failed
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Clock Sync Check Failure',
  'Clock Sync check failed on DB node(s)',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'CLOCK_SYNC_CHECK_FAILED',
  true,
  true
from customer;

select create_universe_alert_definitions('Clock Sync Check Failure');
