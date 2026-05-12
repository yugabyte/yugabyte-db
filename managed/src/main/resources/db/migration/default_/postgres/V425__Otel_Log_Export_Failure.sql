-- Copyright (c) YugabyteDB, Inc.

-- OTel Log Export Failure alert
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'OTel Log Export Failure',
  'OpenTelemetry log records are not being exported from DB nodes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'OTEL_LOG_EXPORT_FAILURE',
  300,
  true,
  true
from customer;

select create_universe_alert_definitions('OTel Log Export Failure');

