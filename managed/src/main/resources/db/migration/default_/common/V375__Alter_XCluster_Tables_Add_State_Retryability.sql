-- Copyright (c) YugaByte, Inc.

-- Add new states required for retryability to the possible states
-- for xcluster_config.status.
ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS ck_xcluster_config_status;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT ck_xcluster_config_status
    CHECK (status IN ('Initialized',
                      'Running',
                      'Updating',
                      'DeletedUniverse',
                      'DeletionFailed',
                      'Failed',
                      'DrainedData'));

-- Update Error state to Failed for Dr configs.
UPDATE dr_config
    SET state = 'Failed'
    WHERE state = 'Error';

-- A PITR config can belong to multiple xCluster configs.
ALTER TABLE IF EXISTS xcluster_pitr
    DROP CONSTRAINT IF EXISTS uq_xcluster_pitr_pitr_uuid;
