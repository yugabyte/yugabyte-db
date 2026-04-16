-- Copyright (c) YugaByte, Inc.

ALTER TABLE node_agent DROP CONSTRAINT IF EXISTS uq_node_agent_ip;
ALTER TABLE node_agent ADD CONSTRAINT uq_customer_node_agent_ip UNIQUE(customer_uuid, ip);
ALTER TABLE node_agent ALTER COLUMN name SET DEFAULT 'default';
ALTER TABLE node_agent ALTER COLUMN name SET NOT NULL;
