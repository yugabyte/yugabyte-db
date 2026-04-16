-- Copyright (c) YugaByte, Inc.

-- Add type to the xcluster_config table.
ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS type VARCHAR(256) DEFAULT 'Basic' NOT NULL;

ALTER TABLE IF EXISTS xcluster_config
  DROP CONSTRAINT IF EXISTS ck_xcluster_config_type;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT ck_xcluster_config_type
    CHECK (type IN (
        'Basic',
        'Txn'
    ));

-- Add extra columns specific to txn xCluster configs.
ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS source_active BOOLEAN DEFAULT TRUE NOT NULL;

ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS target_active BOOLEAN DEFAULT TRUE NOT NULL;

ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS txn_table_id VARCHAR(256);

ALTER TABLE IF EXISTS xcluster_config
    ADD COLUMN IF NOT EXISTS txn_table_replication_group_name VARCHAR(256);

ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS fk_xcluster_config_txn_table_id;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT fk_xcluster_config_txn_table_id
    FOREIGN KEY (uuid, txn_table_id) REFERENCES xcluster_table_config(config_uuid, table_id)
    ON UPDATE CASCADE;
