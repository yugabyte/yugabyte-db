-- Copyright (c) YugaByte, Inc.

ALTER TABLE schedule ADD COLUMN IF NOT EXISTS next_schedule_task_time timestamp;

create view schedule_time_helper as SELECT DISTINCT ON (schedule_uuid) schedule_uuid, scheduled_time FROM schedule_task  ORDER by schedule_uuid , schedule_task.scheduled_time  desc;

update schedule set next_schedule_task_time = ((select schedule_time_helper.scheduled_time from schedule_time_helper where schedule.schedule_uuid =  schedule_time_helper.schedule_uuid)::timestamp + frequency  * INTERVAL '0.001' SECOND ) where schedule.cron_expression is null;

drop view schedule_time_helper;

ALTER TABLE schedule ADD COLUMN IF NOT EXISTS backlog_status boolean DEFAULT FALSE NOT NULL;
