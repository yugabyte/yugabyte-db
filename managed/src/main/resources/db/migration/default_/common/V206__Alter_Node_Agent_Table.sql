-- Copyright (c) YugaByte, Inc.

ALTER TABLE node_agent ADD COLUMN IF NOT EXISTS port integer NOT NULL;
ALTER TABLE node_agent ALTER COLUMN version TYPE VARCHAR(50);
