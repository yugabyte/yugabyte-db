-- Copyright (c) YugaByte, Inc.

-- Add replication_setup_time for basic and txn replication.
ALTER TABLE IF EXISTS xcluster_table_config
    ADD COLUMN IF NOT EXISTS replication_setup_time TIMESTAMP;

-- Add replication_setup_time for db-scoped replication.
ALTER TABLE IF EXISTS xcluster_namespace_config
    ADD COLUMN IF NOT EXISTS replication_setup_time TIMESTAMP;

