-- Copyright (c) YugabyteDB, Inc.

-- No health check of the universe has completed for several health check intervals.
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds,
   threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Health check not running',
  'No health check of the universe has completed for several health check intervals, so the '
    || 'alerts based on health check results can''t fire for it',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":3.0}}',
  'COUNT',
  'HEALTH_CHECK_NOT_RUNNING',
  0,
  true,
  true
from customer;

select create_universe_alert_definitions('Health check not running');

-- A YBA scheduler has not completed a run for several of its intervals.
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds,
   threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'YBA scheduler not running',
  'A YBA scheduler has not completed a run for several of its intervals, so the background job '
    || 'it runs is stalled',
  current_timestamp,
  'PLATFORM',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":3.0}}',
  'COUNT',
  'YBA_SCHEDULER_NOT_RUNNING',
  0,
  true,
  true
from customer;

select create_customer_alert_definitions('YBA scheduler not running', false);
