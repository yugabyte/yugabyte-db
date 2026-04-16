-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS source_universe_state VARCHAR(512);

ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS target_universe_state VARCHAR(512);

ALTER TABLE IF EXISTS dr_config
    ADD COLUMN IF NOT EXISTS state VARCHAR(512);
