-- Copyright (c) YugaByte, Inc.

-- Default value gets corrected on upgrade. It is set as a placeholder only.
ALTER TABLE node_agent ADD COLUMN IF NOT EXISTS home VARCHAR(1024) default '/home/yugabyte/node-agent' NOT NULL;
