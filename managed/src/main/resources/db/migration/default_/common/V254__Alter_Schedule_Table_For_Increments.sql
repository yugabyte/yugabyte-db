-- Copyright (c) YugaByte, Inc.
ALTER TABLE IF EXISTS schedule ADD COLUMN 
    IF NOT EXISTS next_increment_schedule_task_time timestamp;

ALTER TABLE IF EXISTS schedule ADD COLUMN
    IF NOT EXISTS increment_backlog_status boolean DEFAULT FALSE NOT NULL;
