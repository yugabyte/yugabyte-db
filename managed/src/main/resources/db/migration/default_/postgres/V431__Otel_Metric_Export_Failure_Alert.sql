-- Copyright (c) YugabyteDB, Inc.

-- OTel Metric Export Failure alert - DB metrics not getting exported
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'OTel Metric Export Failure',
  'DB metrics are not being exported for universe',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'OTEL_METRIC_EXPORT_FAILURE',
  300,
  true,
  true
from customer;

select create_universe_alert_definitions('OTel Metric Export Failure');
