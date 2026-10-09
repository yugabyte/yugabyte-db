-- Copyright (c) YugaByte, Inc.

-- Regenerate DB_INSTANCE_RESTART: the query now filters on master/tserver.
UPDATE alert_definition SET config_written = false WHERE configuration_uuid IN
(SELECT uuid from alert_configuration where template = 'DB_INSTANCE_RESTART');

-- OTel Collector restart alert.
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target,
   thresholds, threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'OTel Collector restart',
  'Unexpected OpenTelemetry collector restart(s) occurred during last 30 minutes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":0.0},
    "SEVERE":{"condition":"GREATER_THAN", "threshold":2.0}}',
  'COUNT',
  'OTEL_COLLECTOR_RESTART',
  0,
  true,
  true
from customer;

select create_universe_alert_definitions('OTel Collector restart');

-- Regenerate OTEL_METRIC_EXPORT_FAILURE: the threshold comparison moved inside count().
UPDATE alert_definition SET config_written = false WHERE configuration_uuid IN
(SELECT uuid from alert_configuration where template = 'OTEL_METRIC_EXPORT_FAILURE');
