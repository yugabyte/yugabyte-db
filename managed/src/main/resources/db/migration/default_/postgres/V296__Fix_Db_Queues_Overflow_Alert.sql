-- Copyright (c) YugaByte, Inc.

update alert_configuration set thresholds = REPLACE(thresholds, 'SEVERE', 'WARNING') where template = 'DB_QUEUES_OVERFLOW';
update alert_definition set config_written = false where configuration_uuid IN (select uuid from alert_configuration where template = 'DB_QUEUES_OVERFLOW'); 