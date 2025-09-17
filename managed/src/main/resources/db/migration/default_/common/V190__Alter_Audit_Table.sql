-- Copyright (c) YugabyteDB, Inc.
ALTER TABLE audit ADD COLUMN IF NOT EXISTS additional_details TEXT;