-- Copyright (c) YugaByte, Inc.

-- Add first_snapshot_time to backup table
delete from alert_configuration where template = 'DB_QUEUES_OVERFLOW';
