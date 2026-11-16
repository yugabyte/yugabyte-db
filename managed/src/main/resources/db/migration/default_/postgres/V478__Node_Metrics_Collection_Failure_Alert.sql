-- Copyright (c) YugabyteDB, Inc.

-- DB node metrics written for the node_exporter textfile collector are missing, stale or can't be
-- read, so the alerts built on them can't fire.
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds,
   threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DB node metrics collection failure',
  'Node metrics collected through the node_exporter textfile collector are missing, stale or '
    || 'can''t be read on DB node(s), so alerts based on them can''t fire for those nodes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'NODE_METRICS_COLLECTION_FAILURE',
  600,
  true,
  true
from customer;

select create_universe_alert_definitions('DB node metrics collection failure', 'VM');
