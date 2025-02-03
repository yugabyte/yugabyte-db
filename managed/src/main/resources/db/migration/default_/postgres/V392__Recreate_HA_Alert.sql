-- Copyright (c) YugaByte, Inc.

UPDATE alert_configuration
SET threshold_unit = 'MINUTE',
    thresholds = jsonb_set(
        thresholds::jsonb,
        '{WARNING,threshold}',
        to_jsonb((thresholds::jsonb -> 'WARNING' ->> 'threshold')::double precision / 60)
    )
WHERE template = 'HA_STANDBY_SYNC';

UPDATE alert_definition SET config_written = false WHERE configuration_uuid IN
(SELECT uuid from alert_configuration where template = 'HA_STANDBY_SYNC');
