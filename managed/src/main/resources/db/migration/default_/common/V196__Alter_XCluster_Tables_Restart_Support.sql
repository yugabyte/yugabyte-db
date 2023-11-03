-- Copyright (c) YugaByte, Inc.

-- Rename Deleted to DeletionFailed and Init to Initialized in the possible states for
-- xcluster_config.status.
ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS ck_xcluster_config_status;

UPDATE xcluster_config SET status = 'Initialized' WHERE status = 'Init';

UPDATE xcluster_config SET status = 'DeletionFailed' WHERE status = 'Deleted';

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT ck_xcluster_config_status
    CHECK (status IN (
        'Initialized',
        'Running',
        'Updating',
        'DeletedUniverse',
        'DeletionFailed',
        'Failed'
    ));


