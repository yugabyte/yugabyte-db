-- Copyright (c) YugaByte, Inc.

ALTER TABLE schedule ADD COLUMN IF NOT EXISTS running_state boolean DEFAULT FALSE NOT NULL;
