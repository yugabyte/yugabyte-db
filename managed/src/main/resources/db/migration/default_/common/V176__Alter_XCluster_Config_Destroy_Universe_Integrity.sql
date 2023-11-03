-- Copyright (c) YugaByte, Inc.

-- Add UniverseDeleted to the possible states for xcluster_config.status.
ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS ck_xcluster_config_status;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT ck_xcluster_config_status
    CHECK (status IN ('Init',
                      'Running',
                      'Updating',
                      'Paused',
                      'Deleted',
                      'DeletedUniverse',
                      'Failed'));

-- Add replication_group_name column so that the source uuid can be null.
ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS replication_group_name VARCHAR(256);

-- Automatically update the added column.
UPDATE xcluster_config
    SET replication_group_name = concat_ws('_', source_universe_uuid, config_name);

-- Add NOT NULL constraint for the added column.
ALTER TABLE IF EXISTS xcluster_config
    ALTER COLUMN replication_group_name SET NOT NULL;

-- Universe UUIDs in xcluster_config table can be null only when status is DeletedUniverse.
ALTER TABLE IF EXISTS xcluster_config
    ALTER COLUMN source_universe_uuid DROP NOT NULL;

ALTER TABLE IF EXISTS xcluster_config
    ALTER COLUMN target_universe_uuid DROP NOT NULL;

ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS ck_source_universe_uuid_not_null_or_status_is_deleted_universe;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT ck_source_universe_uuid_not_null_or_status_is_deleted_universe
    CHECK (source_universe_uuid IS NOT NULL OR status = 'DeletedUniverse');

ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS ck_target_universe_uuid_not_null_or_status_is_deleted_universe;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT ck_target_universe_uuid_not_null_or_status_is_deleted_universe
    CHECK (target_universe_uuid IS NOT NULL OR status = 'DeletedUniverse');

-- Change the universes' UUIDs foreign key constraints such that it sets
-- them to null when the universe is deleted.
ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS fk_xcluster_config_source_universe_uuid;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT fk_xcluster_config_source_universe_uuid
    FOREIGN KEY (source_universe_uuid) REFERENCES universe(universe_uuid)
    ON UPDATE CASCADE
    ON DELETE SET NULL;

ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS fk_xcluster_config_target_universe_uuid;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT fk_xcluster_config_target_universe_uuid
    FOREIGN KEY (target_universe_uuid) REFERENCES universe(universe_uuid)
    ON UPDATE CASCADE
    ON DELETE SET NULL;
