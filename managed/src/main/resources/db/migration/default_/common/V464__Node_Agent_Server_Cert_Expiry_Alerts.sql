-- Copyright (c) YugabyteDB, Inc.

-- Universe-scoped: node agents attached to universe nodes
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds,
   threshold_unit, template, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'Node agent server cert expiry',
  'Node agent server certificate expires soon on a universe node',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":30.0}}',
  'DAY',
  'NODE_AGENT_SERVER_CERT_EXPIRY',
  true,
  true
FROM customer;

SELECT create_universe_alert_definitions('Node agent server cert expiry');

-- Platform-scoped: node agents not associated with a universe
INSERT INTO alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds,
   threshold_unit, template, active, default_destination)
SELECT
  gen_random_uuid(),
  uuid,
  'Non-universe node agent server cert expiry',
  'Node agent server certificate expires soon on a node agent not associated with a universe',
  current_timestamp,
  'PLATFORM',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":30.0}}',
  'DAY',
  'NODE_AGENT_SERVER_CERT_EXPIRY_NON_UNIVERSE',
  true,
  true
FROM customer;

SELECT create_customer_alert_definitions('Non-universe node agent server cert expiry', false);
