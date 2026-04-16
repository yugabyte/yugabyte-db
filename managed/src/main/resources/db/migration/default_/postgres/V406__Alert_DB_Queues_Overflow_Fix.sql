-- Copyright (c) YugaByte, Inc.

-- Update DB_QUEUES_OVERFLOW alert threshold
UPDATE alert_configuration
SET thresholds = jsonb_set(
        thresholds::jsonb,
        '{WARNING,threshold}',
        to_jsonb(greatest((thresholds::jsonb -> 'WARNING' ->> 'threshold')::double precision, 100))
    )
WHERE template = 'DB_QUEUES_OVERFLOW';

-- Update DB_QUEUES_OVERFLOW alert duration
UPDATE alert_configuration
SET duration_sec = greatest(duration_sec, 300)
WHERE template = 'DDL_ATOMICITY_CHECK';

UPDATE alert_definition
SET config_written = false
WHERE configuration_uuid IN
  (select uuid from alert_configuration where template in ('DB_QUEUES_OVERFLOW', 'DDL_ATOMICITY_CHECK'));
