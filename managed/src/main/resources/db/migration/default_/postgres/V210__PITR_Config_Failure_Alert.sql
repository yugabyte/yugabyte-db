-- PITR_CONFIG_FAILURE
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'PITR Config Failure',
  'Last snapshot task failed for universe',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'PITR_CONFIG_FAILURE',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'PITR Config Failure',
 'min(ybp_pitr_config_status{universe_uuid = "__universeUuid__"})'
    || ' {{ query_condition }} {{ query_threshold }}');
