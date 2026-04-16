-- Copyright (c) YugaByte, Inc.

-- Recreate alert definition to fix affected nodes expression
update alert_definition set config_written = false where configuration_uuid IN
  (select uuid from alert_configuration where template in ('DB_ERROR_LOGS','DB_FATAL_LOGS'));
