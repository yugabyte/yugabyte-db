-- Copyright (c) YugaByte, Inc.

ALTER TABLE node_agent ADD COLUMN IF NOT EXISTS state varchar(30) NOT NULL;
DROP INDEX IF EXISTS ix_node_agent_state_version;
CREATE INDEX ix_node_agent_state_version on node_agent (state, version);
