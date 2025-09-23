-- Copyright (c) YugaByte, Inc.

--- Update default threshold
update alert_configuration
set thresholds = replace(thresholds,
                         '"SEVERE":{"condition":"GREATER_THAN","threshold":500.0}',
                         '"SEVERE":{"condition":"GREATER_THAN","threshold":250.0}')
where template = 'CLOCK_SKEW';

-- Recreate alert definition with new threshold
update alert_definition set config_written = false where configuration_uuid IN
  (select uuid from alert_configuration where template = 'CLOCK_SKEW');
