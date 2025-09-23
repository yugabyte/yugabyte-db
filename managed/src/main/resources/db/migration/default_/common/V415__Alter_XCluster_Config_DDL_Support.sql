-- Copyright (c) YugaByte, Inc.

-- Add automaticDdlMode column to the xcluster_config table.
ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS automatic_ddl_mode BOOLEAN DEFAULT FALSE NOT NULL;
