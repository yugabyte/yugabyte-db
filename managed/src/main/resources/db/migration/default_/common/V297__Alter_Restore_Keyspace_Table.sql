-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS restore_keyspace
      ADD COLUMN IF NOT EXISTS table_name_list TEXT;