-- Copyright (c) YugaByte, Inc.
ALTER TABLE provider ADD COLUMN IF NOT EXISTS update_source INTEGER;
ALTER TABLE provider ADD COLUMN IF NOT EXISTS prev_usability_state INTEGER;
