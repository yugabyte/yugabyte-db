-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS restore DROP CONSTRAINT IF EXISTS restore_task_uuid_key;
ALTER TABLE IF EXISTS xcluster_table_config ADD COLUMN IF NOT EXISTS restore_uuid UUID;

ALTER TABLE IF EXISTS xcluster_table_config
    DROP CONSTRAINT IF EXISTS fk_xcluster_table_config_restore_uuid;

ALTER TABLE IF EXISTS xcluster_table_config
    ADD CONSTRAINT fk_xcluster_table_config_restore_uuid
    FOREIGN KEY (restore_uuid) REFERENCES restore (restore_uuid)
    ON DELETE SET NULL
    ON UPDATE CASCADE;
