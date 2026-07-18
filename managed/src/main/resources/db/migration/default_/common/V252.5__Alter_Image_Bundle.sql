-- Copyright (c) YugaByte, Inc.

-- Default Image Bundle Name can we quite large as we append provider name
-- with prefix for the default image bundle name.
ALTER TABLE IF EXISTS image_bundle ALTER COLUMN name TYPE VARCHAR(512);