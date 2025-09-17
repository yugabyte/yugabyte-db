-- Copyright (c) YugabyteDB, Inc.

ALTER TABLE node_agent ADD COLUMN IF NOT EXISTS last_error TEXT;
