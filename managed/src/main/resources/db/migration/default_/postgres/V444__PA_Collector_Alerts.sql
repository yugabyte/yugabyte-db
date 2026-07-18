-- Copyright (c) YugabyteDB, Inc.

-- PA Collector Down alert (PLATFORM)
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, duration_sec, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'PA Collector Down',
  'Performance Advisor collector is expected to be running but is not reporting metrics',
  current_timestamp,
  'PLATFORM',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1.0}}',
  'STATUS',
  'PA_COLLECTOR_DOWN',
  900,
  true,
  true
FROM customer;

SELECT create_customer_alert_definitions('PA Collector Down', false);

-- PA Universe Collection Failure alert (UNIVERSE)
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, duration_sec, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'PA Universe Collection Failure',
  'Performance Advisor data collection for universe is failing',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'PA_UNIVERSE_COLLECTION_FAILURE',
  1800,
  true,
  true
FROM customer;

SELECT create_universe_alert_definitions('PA Universe Collection Failure');

-- PA Universe Collection Stale alert (UNIVERSE)
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'PA Universe Collection Stale',
  'Performance Advisor data collection for universe is taking longer than expected',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'PA_UNIVERSE_COLLECTION_STALE',
  true,
  true
FROM customer;

SELECT create_universe_alert_definitions('PA Universe Collection Stale');

-- PA Embedded Collector Error alert (PLATFORM)
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'PA Embedded Collector Error',
  'Embedded Performance Advisor collector initialization failed',
  current_timestamp,
  'PLATFORM',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1.0}}',
  'STATUS',
  'PA_EMBEDDED_COLLECTOR_ERROR',
  true,
  true
FROM customer;

SELECT create_customer_alert_definitions('PA Embedded Collector Error', false);
