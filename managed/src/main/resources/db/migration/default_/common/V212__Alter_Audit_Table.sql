-- Copyright (c) YugaByte, Inc.
ALTER TABLE audit ADD COLUMN IF NOT EXISTS user_address varchar(64);
