-- Copyright (c) YugaByte, Inc.

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'definition_name', 'Alert Notification Failed' from alert a
where a.err_code = 'ALERT_MANAGER_FAILURE' and a.group_uuid is null);

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'definition_name', 'Inactive Cronjob Nodes' from alert a
where a.err_code = 'CRON_CREATION_FAILURE' and a.group_uuid is null);

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'definition_name', 'Backup Failure' from alert a
where a.err_code = 'TASK_FAILURE' and a.group_uuid is null);

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'definition_name', 'Health Check Error' from alert a
where a.err_code = 'HEALTH_CHECKER_FAILURE' and a.group_uuid is null);

alter table alert drop column if exists err_code;
delete from alert_label where name = 'error_code';
delete from alert_definition_label where name = 'error_code';

alter table alert drop column if exists send_email;

-- Otherwise they will never be resolved.
update alert
 set state = 'RESOLVED', target_state = 'RESOLVED', resolved_time = current_timestamp
 where group_uuid is null and state != 'RESOLVED';