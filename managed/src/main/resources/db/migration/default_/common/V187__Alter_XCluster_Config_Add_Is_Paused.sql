-- Copyright (c) YugaByte, Inc.

-- Add paused column.
ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS paused BOOLEAN DEFAULT false NOT NULL;

UPDATE xcluster_config SET paused = true, status = 'Running' WHERE status = 'Paused';

-- Remove Paused from the possible states for xcluster_config.status.
ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS ck_xcluster_config_status;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT ck_xcluster_config_status
    CHECK (status IN ('Init',
                      'Running',
                      'Updating',
                      'Deleted',
                      'DeletedUniverse',
                      'Failed'));
