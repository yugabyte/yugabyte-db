-- Copyright (c) YugaByte, Inc.

-- Set the XCLUSTER_CONFIG_TABLE_BAD_STATE alert duration to 3 minutes for configurations
-- that still use the old default of 0 seconds.
UPDATE alert_configuration
SET duration_sec = 180
WHERE template = 'XCLUSTER_CONFIG_TABLE_BAD_STATE' AND duration_sec = 0;

UPDATE alert_definition
SET config_written = false
WHERE configuration_uuid IN
  (select uuid from alert_configuration where template = 'XCLUSTER_CONFIG_TABLE_BAD_STATE');
