-- Copyright (c) YugabyteDB, Inc.

-- CDCSDK_EXPIRY
UPDATE alert_definition
SET config_written = false
WHERE configuration_uuid IN
  (SELECT uuid FROM alert_configuration WHERE template = 'CDCSDK_EXPIRY');