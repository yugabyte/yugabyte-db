-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS dr_config ADD COLUMN IF NOT EXISTS pitr_retention_period_sec BIGINT;
ALTER TABLE IF EXISTS dr_config ADD COLUMN IF NOT EXISTS pitr_snapshot_interval_sec BIGINT;
