-- Copyright (c) YugaByte, Inc.

-- Add status column to the xcluster_namespace_config table.
ALTER TABLE IF EXISTS xcluster_namespace_config
    ADD COLUMN IF NOT EXISTS status VARCHAR(32) DEFAULT 'Running' NOT NULL;

-- Add check constraint for possible values of xcluster_namespace_config.status.
ALTER TABLE IF EXISTS xcluster_namespace_config
    DROP CONSTRAINT IF EXISTS ck_xcluster_namespace_config_table_status;

ALTER TABLE IF EXISTS xcluster_namespace_config
    ADD CONSTRAINT ck_xcluster_namespace_config_table_status
    CHECK (status IN ('Validated', -- Initialized.
                      'Running',
                      'Updating',
                      'Bootstrapping',
                      'Failed')); -- Failed to add, create and delete.