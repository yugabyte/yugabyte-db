-- Copyright (c) YugabyteDB, Inc.

-- CDCSDK_FLUSH_LAG
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'CDC SDK Flush Lag',
  'Max CDC SDK replication slot flush lag for 10 minutes in ms is above threshold',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":21600000.0}}',
  'MILLISECOND',
  'CDCSDK_FLUSH_LAG',
  true,
  true
FROM customer;

SELECT create_universe_alert_definitions('CDC SDK Flush Lag');

-- CDCSDK_EXPIRY
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'CDC SDK Replication Slot Expiry',
  'CDC SDK replication slot expiry status (expired or near expiry)',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1.0}, "WARNING":{"condition":"LESS_THAN", "threshold":7200000.0}}',
  'MILLISECOND',
  'CDCSDK_EXPIRY',
  true,
  true
FROM customer;

SELECT create_universe_alert_definitions('CDC SDK Replication Slot Expiry');

-- CDCSDK_IDLE_STREAM
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'CDC SDK Idle Stream',
  'CDC SDK replication slot is idle (no messages emitted, change event count rate is 0)',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'CDCSDK_IDLE_STREAM',
  true,
  true
FROM customer;

SELECT create_universe_alert_definitions('CDC SDK Idle Stream');
