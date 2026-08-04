-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS node_instance ADD COLUMN if NOT EXISTS state VARCHAR(20) DEFAULT 'free' NOT NULL;


UPDATE node_instance SET state = 
    CASE 
      WHEN in_use = true THEN 'USED'
      WHEN in_use = false THEN 'FREE'
      ELSE state 
    END;

ALTER TABLE IF EXISTS node_instance DROP COLUMN IF EXISTS in_use;

ALTER TABLE IF EXISTS node_instance
    DROP CONSTRAINT IF EXISTS ck_node_instance_state;

ALTER TABLE IF EXISTS node_instance
    ADD CONSTRAINT ck_node_instance_state
    CHECK (state in ('USED',
                     'FREE',
                     'DECOMMISSIONED'));

ALTER TABLE IF EXISTS node_instance ADD COLUMN if NOT EXISTS universe_metadata TEXT;
