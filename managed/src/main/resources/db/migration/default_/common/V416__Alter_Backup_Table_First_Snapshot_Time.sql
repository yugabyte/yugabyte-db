-- Copyright (c) YugaByte, Inc.

-- Add first_snapshot_time to backup table
ALTER TABLE IF EXISTS backup
    ADD COLUMN IF NOT EXISTS first_snapshot_time bigint DEFAULT 0;
