-- XCLUSTER_CONFIG_TABLE_STATUS_FAILURE

insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'XCluster Config Table Bad State',
  'XCluster config table status are in bad state',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":0}}',
  'STATUS',
  'XCLUSTER_CONFIG_TABLE_BAD_STATE',
  true,
  true
from customer;

select create_universe_alert_definitions('XCluster Config Table Bad State');

-- Delete the old alert configuration for NEW_YSQL_TABLES_ADDED
delete from alert_configuration where template = 'NEW_YSQL_TABLES_ADDED';
