-- Copyright (c) YugabyteDB, Inc.

-- When the config was last saved: the revision of a storage config in the v2 API (its ETag, and
-- what an edit's task checks before applying it). Existing configs start at the upgrade.
ALTER TABLE customer_config ADD COLUMN IF NOT EXISTS update_time TIMESTAMP NOT NULL DEFAULT now();
