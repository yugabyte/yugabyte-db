-- Copyright (c) YugabyteDB, Inc.

ALTER TABLE IF EXISTS xcluster_config ADD COLUMN IF NOT EXISTS secondary BOOLEAN DEFAULT false NOT NULL;
