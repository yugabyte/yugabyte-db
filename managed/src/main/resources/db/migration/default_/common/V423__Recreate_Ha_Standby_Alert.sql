-- Copyright (c) YugaByte, Inc.

UPDATE alert_definition SET config_written = false WHERE configuration_uuid IN
(SELECT uuid from alert_configuration where template = 'HA_STANDBY_SYNC');