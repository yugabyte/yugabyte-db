-- Copyright (c) YugaByte, Inc.

-- Continuous Backup Alert
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'Continuous Backups Failing',
  'The most recent backup uploaded to remote storage was successful',
  current_timestamp,
  'PLATFORM',
  '{"all":true}',
  '{"WARNING":{"condition":"LESS_THAN", "threshold":1.0}}',
  'STATUS',
  'CONTINUOUS_BACKUPS_STATUS',
  true,
  true
FROM customer;

SELECT create_customer_alert_definitions('Continuous Backups Failing', false);
