-- Copyright (c) YugabyteDB, Inc.

-- Regenerate NODE_MAINTENANCE_SERVICE_NOT_RUN: the query now uses the same window as its node list.
UPDATE alert_definition SET config_written = false WHERE configuration_uuid IN
(SELECT uuid from alert_configuration where template = 'NODE_MAINTENANCE_SERVICE_NOT_RUN');
