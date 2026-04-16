-- Copyright (c) YugaByte, Inc.

 -- Tablet peers guardrail
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Percentage of tablet peers is high',
  'It represents the percentage of number of live tablet peers compared to number of tablet peers that can be supported based on available RAM, cores, etc.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN","threshold":90.0},"SEVERE":{"condition":"GREATER_THAN", "threshold":100.0}}',
  'PERCENT',
  'TABLET_PEERS_GUARDRAIL',
  1200,
  true,
  true
from customer;

select create_universe_alert_definitions('Percentage of tablet peers is high');
