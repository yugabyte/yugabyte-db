-- Copyright (c) YugabyteDB, Inc.

-- Alerts for the YB node maintenance systemd services
-- (yb-zip_purge_yb_logs, yb-clean_cores) on universe DB nodes.

-- Last execution of a maintenance service failed.
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds,
   threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Node maintenance service failed',
  'Last execution of a YB node maintenance systemd service '
    || '(yb-zip_purge_yb_logs / yb-clean_cores) failed on DB node(s).',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'NODE_MAINTENANCE_SERVICE_FAILED',
  true,
  true
from customer;

select create_universe_alert_definitions('Node maintenance service failed');

-- Maintenance service has not run within the expected window.
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds,
   threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Node maintenance service not running',
  'A YB node maintenance systemd service '
    || '(yb-zip_purge_yb_logs / yb-clean_cores) has not been executed recently or never ran.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'NODE_MAINTENANCE_SERVICE_NOT_RUN',
  true,
  true
from customer;

select create_universe_alert_definitions('Node maintenance service not running');

-- Maintenance service unit definition is missing on the node.
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds,
   threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Node maintenance service definition missing',
  'YB node maintenance systemd service '
    || '(yb-zip_purge_yb_logs / yb-clean_cores) unit definition is missing on the node.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'NODE_MAINTENANCE_SERVICE_DEFINITION_MISSING',
  true,
  true
from customer;

select create_universe_alert_definitions('Node maintenance service definition missing');
