-- Copyright (c) YugabyteDB, Inc.

-- Add imported column.
ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS imported BOOLEAN DEFAULT false NOT NULL;
