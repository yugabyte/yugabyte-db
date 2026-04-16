-- Copyright (c) YugaByte, Inc.

-- Set replication_setup_done to true for existing tables in replication that did not need
-- bootstrap.
UPDATE xcluster_table_config SET replication_setup_done = true WHERE need_bootstrap = false;
