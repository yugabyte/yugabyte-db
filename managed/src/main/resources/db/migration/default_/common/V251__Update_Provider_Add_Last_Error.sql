-- Copyright (c) YugaByte, Inc.

ALTER TABLE provider ADD COLUMN IF NOT EXISTS last_validation_errors TEXT;
ALTER TABLE provider ADD COLUMN IF NOT EXISTS usability_state INTEGER default 0;
