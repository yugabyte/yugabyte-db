-- Copyright (c) YugaByte, Inc.


-- Add backup (backup_uuid) relation.
ALTER TABLE IF EXISTS xcluster_namespace_config
    ADD COLUMN IF NOT EXISTS backup_uuid UUID;

ALTER TABLE IF EXISTS xcluster_namespace_config
    DROP CONSTRAINT IF EXISTS fk_xcluster_namespace_config_backup_uuid;

ALTER TABLE IF EXISTS xcluster_namespace_config
    ADD CONSTRAINT fk_xcluster_namespace_config_backup_uuid
    FOREIGN KEY (backup_uuid) REFERENCES backup (backup_uuid)
    ON DELETE SET NULL
    ON UPDATE CASCADE;


-- Add restore (restore_uuid) relation.
ALTER TABLE IF EXISTS xcluster_namespace_config
    ADD COLUMN IF NOT EXISTS restore_uuid UUID;

ALTER TABLE IF EXISTS xcluster_namespace_config
    DROP CONSTRAINT IF EXISTS fk_xcluster_namespace_config_restore_uuid;

ALTER TABLE IF EXISTS xcluster_namespace_config
    ADD CONSTRAINT fk_xcluster_namespace_config_restore_uuid
    FOREIGN KEY (restore_uuid) REFERENCES restore (restore_uuid)
    ON DELETE SET NULL
    ON UPDATE CASCADE;
