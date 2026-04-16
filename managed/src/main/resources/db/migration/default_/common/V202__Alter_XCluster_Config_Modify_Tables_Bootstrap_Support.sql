-- Copyright (c) YugaByte, Inc.

-- Add table_type column to the xcluster_config table.
ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS table_type VARCHAR(32) DEFAULT 'UNKNOWN' NOT NULL;

-- Add check constraint for possible values of xcluster_config.table_type.
ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS ck_xcluster_config_table_type;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT ck_xcluster_config_table_type
    CHECK (table_type IN ('UNKNOWN',
                          'YSQL',
                          'YCQL'));

-- Add index_table column to the xcluster_table_config table.
ALTER TABLE IF EXISTS xcluster_table_config
    ADD COLUMN IF NOT EXISTS index_table BOOLEAN DEFAULT false NOT NULL;
