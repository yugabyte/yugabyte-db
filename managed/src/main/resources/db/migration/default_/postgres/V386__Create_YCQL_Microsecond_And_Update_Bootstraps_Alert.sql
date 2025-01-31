update alert_configuration
  set
    threshold_unit = 'PERCENT',
    thresholds = '{"WARNING":{"condition":"GREATER_THAN","threshold":10.0}}'
  where template = 'INCREASED_REMOTE_BOOTSTRAPS';

update alert_definition set config_written = false where configuration_uuid IN
  (select uuid from alert_configuration where template = 'INCREASED_REMOTE_BOOTSTRAPS');

 -- YCQL microseconds precision inserts detected alert
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'YCQL inserts with microseconds precision',
  'YCQL inserts with microseconds precision detected, which is not fully supported.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN","threshold":60.0}}',
  'MINUTE',
  'YCQL_MICROSECOND_TIMESTAMPS_DETECTED',
  0,
  true,
  true
from customer;

select create_universe_alert_definitions('YCQL inserts with microseconds precision');
