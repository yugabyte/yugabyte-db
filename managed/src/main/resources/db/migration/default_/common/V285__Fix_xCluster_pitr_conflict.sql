-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS xcluster_pitr
    DROP CONSTRAINT IF EXISTS fk_xcluster_pitr_xcluster_uuid;

ALTER TABLE IF EXISTS xcluster_pitr
    ADD CONSTRAINT fk_xcluster_pitr_xcluster_uuid
    FOREIGN KEY (xcluster_uuid) REFERENCES xcluster_config (uuid)
    ON DELETE CASCADE
    ON UPDATE CASCADE;

ALTER TABLE IF EXISTS xcluster_pitr
    DROP CONSTRAINT IF EXISTS fk_xcluster_pitr_pitr_uuid;

ALTER TABLE IF EXISTS xcluster_pitr
    ADD CONSTRAINT fk_xcluster_pitr_pitr_uuid
    FOREIGN KEY (pitr_uuid) REFERENCES pitr_config (uuid)
    ON DELETE CASCADE
    ON UPDATE CASCADE;
