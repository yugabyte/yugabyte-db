-- Copyright (c) YugaByte, Inc.

-- Found unexpected masters
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Unexpected masters are running in the universe',
  'Found master processes that are not expected to run',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'UNIVERSE_UNEXPECTED_MASTERS_RUNNING',
  true,
  true
from customer;

select create_universe_alert_definitions('Unexpected masters are running in the universe');

-- Unexpected tserver is running on the node
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Unexpected tservers are running in the universe',
  'Found tserver processes that are not expected to run',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'UNIVERSE_UNEXPECTED_TSERVERS_RUNNING',
  true,
  true
from customer;

select create_universe_alert_definitions('Unexpected tservers are running in the universe');
