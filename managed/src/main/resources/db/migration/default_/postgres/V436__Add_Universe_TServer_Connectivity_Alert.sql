-- Copyright (c) YugaByte, Inc.

-- Universe TServer Connectivity Error Alert
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'Universe TServer Connectivity Error',
  'TServer connectivity check reported one or more nodes unreachable',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'UNIVERSE_TSERVER_CONNECTIVITY_ERROR',
  true,
  true
FROM customer;

SELECT create_universe_alert_definitions('Universe TServer Connectivity Error');
