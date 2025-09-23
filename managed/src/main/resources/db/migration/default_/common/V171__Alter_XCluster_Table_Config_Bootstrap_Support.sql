-- Copyright (c) YugaByte, Inc.

-- Add Bootstrapping to the possible states for xcluster_config.status.
ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS ck_xcluster_config_status;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT ck_xcluster_config_status
    CHECK (status IN ('Init',
                      'Running',
                      'Updating',
                      'Paused',
                      'Deleted',
                      'Failed'));

-- Add new columns.
ALTER TABLE IF EXISTS xcluster_table_config
    ADD COLUMN IF NOT EXISTS replication_setup_done BOOLEAN DEFAULT false NOT NULL;

ALTER TABLE IF EXISTS xcluster_table_config
    ADD COLUMN IF NOT EXISTS need_bootstrap BOOLEAN DEFAULT false NOT NULL;

ALTER TABLE IF EXISTS xcluster_table_config
    ADD COLUMN IF NOT EXISTS bootstrap_create_time timestamp;

ALTER TABLE IF EXISTS xcluster_table_config
    ADD COLUMN IF NOT EXISTS backup_uuid UUID;

ALTER TABLE IF EXISTS xcluster_table_config
    ADD COLUMN IF NOT EXISTS restore_time timestamp;

-- Add unique constraint to xcluster_table_config.stream_id.
ALTER TABLE IF EXISTS xcluster_table_config
    DROP CONSTRAINT IF EXISTS uq_xcluster_table_config_stream_id;

ALTER TABLE IF EXISTS xcluster_table_config
    ADD CONSTRAINT uq_xcluster_table_config_stream_id
    UNIQUE (stream_id);

-- Add foreign key constraint from xcluster_table_config.backup_uuid to backup.backup_uuid.
ALTER TABLE IF EXISTS xcluster_table_config
    DROP CONSTRAINT IF EXISTS fk_xcluster_table_config_backup_uuid;

ALTER TABLE IF EXISTS xcluster_table_config
    ADD CONSTRAINT fk_xcluster_table_config_backup_uuid
    FOREIGN KEY (backup_uuid) REFERENCES backup (backup_uuid)
    ON DELETE SET NULL
    ON UPDATE CASCADE;
