-- Copyright (c) YugaByte, Inc.

-- Update expression in CPU usage alerts
update alert_definition set config_written = false
 where configuration_uuid IN
  (select uuid from alert_configuration where template = 'MASTER_UNDER_REPLICATED');
