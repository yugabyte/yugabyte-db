-- Copyright (c) YugaByte, Inc.

-- Change DB_PROCESS_LIMITS alert severity from SEVERE to WARNING
update alert_configuration
set thresholds = replace(thresholds, '"SEVERE"', '"WARNING"')
where template = 'DB_PROCESS_LIMITS';
