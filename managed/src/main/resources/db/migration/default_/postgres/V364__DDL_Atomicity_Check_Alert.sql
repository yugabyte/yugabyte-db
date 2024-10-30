-- Copyright (c) YugaByte, Inc.

 -- DDL Atomicity check
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DDL Atomicity Check Failed',
  'Some failed DDL operations were not atomic, which can cause subsequent backups to require a manual fixup before they can be restored.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1.0}}',
  'STATUS',
  'DDL_ATOMICITY_CHECK',
  true,
  true
from customer;

select create_universe_alert_definitions('DDL Atomicity Check Failed');
