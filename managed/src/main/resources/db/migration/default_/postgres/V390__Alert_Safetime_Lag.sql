-- Copyright (c) YugaByte, Inc.

-- Clock skew alert
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'XCluster Transactional Safe Time Lag',
  'Max xCluster transactional safe time lag for 10 minutes in ms on the target universe is above threshold; Safe time on the target universe has not progressed for a long time',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":180000.0}}',
  'MILLISECOND',
  'SAFETIME_LAG',
  true,
  true
FROM customer;

SELECT create_universe_alert_definitions('XCluster Transactional Safe Time Lag');
