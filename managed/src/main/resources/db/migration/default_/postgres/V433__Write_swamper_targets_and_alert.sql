-- Copyright (c) YugabyteDB, Inc.

UPDATE universe SET swamper_config_written = false;
UPDATE alert_definition
SET config_written = false
WHERE configuration_uuid IN (
  SELECT uuid
  FROM alert_configuration
  WHERE template = 'DB_MEMORY_OVERLOAD'
);
