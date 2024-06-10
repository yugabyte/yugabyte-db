-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS fk_xcluster_config_txn_table_id;

ALTER TABLE IF EXISTS xcluster_config
    DROP COLUMN IF EXISTS txn_table_id;

ALTER TABLE IF EXISTS xcluster_config
    DROP COLUMN IF EXISTS txn_table_replication_group_name;
