-- Copyright (c) YugaByte, Inc.

-- HEALTH_CHECK_ERROR
select replace_configuration_query(
 'HEALTH_CHECK_ERROR',
 'last_over_time(ybp_health_check_status{universe_uuid = "__universeUuid__"}[1d])'
    || ' {{ query_condition }} 1');

-- HEALTH_CHECK_NOTIFICATION_ERROR
select replace_configuration_query(
 'HEALTH_CHECK_NOTIFICATION_ERROR',
 'last_over_time(ybp_health_check_notification_status{universe_uuid = "__universeUuid__"}[1d])'
    || ' {{ query_condition }} 1');

-- BACKUP_FAILURE
select replace_configuration_query(
 'BACKUP_FAILURE',
 'last_over_time(ybp_create_backup_status{universe_uuid = "__universeUuid__"}[1d])'
    || ' {{ query_condition }} 1');

-- BACKUP_SCHEDULE_FAILURE
select replace_configuration_query(
 'BACKUP_SCHEDULE_FAILURE',
 'last_over_time(ybp_schedule_backup_status{universe_uuid = "__universeUuid__"}[1d])'
    || ' {{ query_condition }} 1');

-- ALERT_QUERY_FAILED
select replace_configuration_query(
 'ALERT_QUERY_FAILED',
 'last_over_time(ybp_alert_query_status[1d]) {{ query_condition }} 1');

-- ALERT_CONFIG_WRITING_FAILED
select replace_configuration_query(
 'ALERT_CONFIG_WRITING_FAILED',
 'last_over_time(ybp_alert_config_writer_status[1d]) {{ query_condition }} 1');

-- ALERT_NOTIFICATION_ERROR
select replace_configuration_query(
 'ALERT_NOTIFICATION_ERROR',
 'last_over_time(ybp_alert_manager_status{customer_uuid = "__customerUuid__"}[1d])'
    || ' {{ query_condition }} 1');

-- ALERT_NOTIFICATION_CHANNEL_ERROR
select replace_configuration_query(
 'ALERT_NOTIFICATION_CHANNEL_ERROR',
 'last_over_time(ybp_alert_manager_channel_status{customer_uuid = "__customerUuid__"}[1d])'
    || ' {{ query_condition }} 1');
