-- Copyright (c) YugaByte, Inc.


update alert_definition set config_written = false where configuration_uuid IN
 (select uuid from alert_configuration where template = 'CLOCK_SYNC_CHECK_FAILED');
