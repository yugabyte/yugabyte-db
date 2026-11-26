-- Copyright (c) YugabyteDB, Inc.

-- Universes whose node_exporter TLS is held back by nodes on the root-level unit
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Node exporter TLS off due to root-level nodes',
  'Node-to-node encryption is on but node_exporter serves plain HTTP, because some nodes still run node_exporter from the root-level systemd unit',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'NODE_EXPORTER_TLS_ROOT_NODES',
  300,
  true,
  true
from customer;

select create_universe_alert_definitions('Node exporter TLS off due to root-level nodes');

-- Nodes outside universes still on the root-level unit
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Non-universe node with root-level node exporter',
  'A node not associated with a universe runs node_exporter from the root-level systemd unit, so it can''t be added to a universe that serves node_exporter over TLS',
  current_timestamp,
  'PLATFORM',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":0.0}}',
  'COUNT',
  'NODE_EXPORTER_ROOT_NON_UNIVERSE',
  300,
  true,
  true
from customer;

select create_customer_alert_definitions('Non-universe node with root-level node exporter', false);
