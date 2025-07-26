-- Copyright (c) YugaByte, Inc.

UPDATE alert_definition
SET config_written = false
WHERE configuration_uuid IN (
  SELECT uuid
  FROM alert_configuration
  WHERE template = 'ENCRYPTION_AT_REST_CONFIG_EXPIRY'
);
