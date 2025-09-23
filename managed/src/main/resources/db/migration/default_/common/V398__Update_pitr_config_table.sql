-- Copyright (c) YugaByte, Inc.

-- Add intermittent earliest recoverable time column to pitr_config table
ALTER TABLE IF EXISTS pitr_config
    ADD COLUMN IF NOT EXISTS intermittent_min_recover_time_in_millis BIGINT DEFAULT 0 NOT NULL;