-- Copyright (c) YugaByte, Inc.

-- DB process limits alert
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DB process limits',
  'Incorrect process limits detected on DB TServer/Master instances.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'DB_PROCESS_LIMITS',
  true,
  true
from customer;

select create_universe_alert_definitions('DB process limits');
