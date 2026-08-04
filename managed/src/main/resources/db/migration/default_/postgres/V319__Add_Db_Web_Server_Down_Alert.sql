-- Copyright (c) YugaByte, Inc.

-- DB API web-server down.
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DB YSQL web server down',
  'DB YSQL web server is down for 15 minutes.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_YSQL_WEB_SERVER_DOWN',
  true,
  true
from customer;

select create_universe_alert_definitions('DB YSQL web server down');

 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DB YCQL web server down',
  'DB YCQL web server is down for 15 minutes.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_YCQL_WEB_SERVER_DOWN',
  true,
  true
from customer;

select create_universe_alert_definitions('DB YCQL web server down');
