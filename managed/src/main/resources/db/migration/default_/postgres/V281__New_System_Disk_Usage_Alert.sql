-- Copyright (c) YugaByte, Inc.

-- Rename NODE_DISK_USAGE alert

UPDATE alert_configuration SET name = 'DB node data disk usage'
WHERE name = 'DB node disk usage' and template = 'NODE_DISK_USAGE';

UPDATE alert_configuration SET description = 'Node data disk usage percentage is above threshold'
WHERE description = 'Node Disk usage percentage is above threshold' and template = 'NODE_DISK_USAGE';

UPDATE alert_definition SET config_written = false WHERE configuration_uuid in
  (SELECT uuid FROM alert_configuration WHERE template in ('NODE_DISK_USAGE', 'DB_DRIVE_FAILURE'));

-- NODE_SYSTEM_DISK_USAGE alert
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DB node system disk usage',
  'Node system disk usage percentage is above threshold',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN","threshold":80.0}}',
  'PERCENT',
  'NODE_SYSTEM_DISK_USAGE',
  0,
  true,
  true
from customer
where not exists(
  select * from alert_configuration WHERE template = 'NODE_SYSTEM_DISK_USAGE'
);

-- This is to handle a partial commit with an incorrect threshold unit
update alert_configuration set threshold_unit = 'PERCENT'  WHERE template = 'NODE_SYSTEM_DISK_USAGE';

select create_universe_alert_definitions('DB node system disk usage');
