-- Copyright (c) YugaByte, Inc.

ALTER TABLE node_agent ADD COLUMN IF NOT EXISTS arch_type VARCHAR(15) NULL;
ALTER TABLE node_agent ADD COLUMN IF NOT EXISTS os_type VARCHAR(15) NULL;
UPDATE node_agent SET state = 'READY' WHERE state = 'LIVE';