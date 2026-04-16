-- Copyright (c) YugaByte, Inc.

-- Universe Release File Missing
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Universe Release Files Missing',
  'Local filepath for universe DB version is missing.',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'UNIVERSE_RELEASE_FILES_STATUS',
  true,
  true
from customer;

select create_universe_alert_definitions('Universe Release Files Missing');