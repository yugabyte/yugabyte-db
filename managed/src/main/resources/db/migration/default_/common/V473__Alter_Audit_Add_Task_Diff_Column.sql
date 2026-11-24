-- Copyright (c) YugabyteDB, Inc.

ALTER TABLE audit ADD COLUMN IF NOT EXISTS task_diff TEXT;
