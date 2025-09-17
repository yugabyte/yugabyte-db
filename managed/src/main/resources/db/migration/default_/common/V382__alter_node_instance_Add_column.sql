-- Copyright (c) YugabyteDB, Inc.

ALTER TABLE IF EXISTS node_instance ADD COLUMN if NOT EXISTS manually_decommissioned boolean DEFAULT false NOT NULL; 