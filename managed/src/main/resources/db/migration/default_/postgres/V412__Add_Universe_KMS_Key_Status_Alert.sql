-- Copyright (c) YugaByte, Inc.

-- Universe KMS Key Failed Validation Alert
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'Universe KMS Key Failed Validation',
  'Validation failed for the last active KMS Key on a universe',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1.0}}',
  'STATUS',
  'UNIVERSE_KMS_KEY_STATUS',
  true,
  true
FROM customer;

SELECT create_universe_alert_definitions('Universe KMS Key Failed Validation');
