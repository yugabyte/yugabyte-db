-- Copyright (c) YugaByte, Inc.

-- Add backup_type and backup_details column to restore table

ALTER TABLE IF EXISTS restore
    ADD COLUMN IF NOT EXISTS backup_type varchar(50);

ALTER TABLE IF EXISTS restore
    ADD COLUMN IF NOT EXISTS backup_created_on_date timestamp;
