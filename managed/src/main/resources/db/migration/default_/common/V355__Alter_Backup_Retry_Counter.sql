-- Copyright (c) YugaByte, Inc.

ALTER TABLE backup ADD COLUMN IF NOT EXISTS retry_count int DEFAULT 0 NOT NULL;
