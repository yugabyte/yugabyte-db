-- Copyright (c) YugaByte, Inc.

-- METRIC_COLLECTION_FAILURE_ALERT
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Metric Collection Failure',
  'Metric Collection failed for universe',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'UNIVERSE_METRIC_COLLECTION_FAILURE',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'Metric Collection Failure',
 'last_over_time(ybp_universe_metric_collection_status{universe_uuid = "__universeUuid__"}[1d])'
    || ' {{ query_condition }} 1');
