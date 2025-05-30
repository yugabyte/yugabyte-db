-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS schedule ADD COLUMN IF NOT EXISTS prev_status varchar(10);

alter table if exists schedule drop constraint if exists ck_schedule_prev_status;

alter table if exists schedule add constraint ck_schedule_prev_status
        check (prev_status in ('Active','Stopped') or prev_status is null);