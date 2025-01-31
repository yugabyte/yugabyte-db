-- Copyright (c) YugaByte, Inc.

-- Recreate alert definition to count by
update alert_definition set config_written = false where configuration_uuid IN
  (select uuid from alert_configuration where template in ('DB_INSTANCE_RESTART','NODE_RESTART'));
