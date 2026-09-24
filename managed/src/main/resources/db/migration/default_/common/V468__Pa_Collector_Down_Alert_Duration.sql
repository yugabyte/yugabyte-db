-- Copyright (c) YugaByte, Inc.

-- Shorten the wait before PA Collector Down fires; Prometheus' 5 minute query lookback delta is
-- added to this duration. Only configurations still on the old default are touched.
UPDATE alert_configuration SET duration_sec = 300
 WHERE template = 'PA_COLLECTOR_DOWN' AND duration_sec = 900;

UPDATE alert_definition SET config_written = false WHERE configuration_uuid IN
 (SELECT uuid FROM alert_configuration WHERE template = 'PA_COLLECTOR_DOWN');
