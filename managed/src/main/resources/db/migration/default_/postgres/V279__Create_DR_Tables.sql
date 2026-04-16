-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF NOT EXISTS dr_config (
    uuid                            UUID,
    name                            VARCHAR(256) NOT NULL,
    create_time                     TIMESTAMP NOT NULL,
    modify_time                     TIMESTAMP NOT NULL,
    CONSTRAINT pk_dr_config PRIMARY KEY (uuid),
    CONSTRAINT uq_dr_config_name UNIQUE (name)
);

ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS dr_config_uuid UUID;

ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS fk_xcluster_config_dr_config_uuid;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT fk_xcluster_config_dr_config_uuid
    FOREIGN KEY (dr_config_uuid) REFERENCES dr_config(uuid)
    ON UPDATE CASCADE;

CREATE TABLE IF NOT EXISTS xcluster_pitr (
    xcluster_uuid UUID NOT NULL,
    pitr_uuid UUID NOT NULL,
    CONSTRAINT pk_xcluster_pitr PRIMARY KEY (xcluster_uuid, pitr_uuid),
    CONSTRAINT uq_xcluster_pitr_pitr_uuid UNIQUE (pitr_uuid),
    CONSTRAINT fk_xcluster_pitr_xcluster_uuid FOREIGN KEY (xcluster_uuid) REFERENCES xcluster_config (uuid),
    CONSTRAINT fk_xcluster_pitr_pitr_uuid FOREIGN KEY (pitr_uuid) REFERENCES pitr_config (uuid)
);
