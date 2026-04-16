-- Copyright (c) YugabyteDB, Inc.

-- Set state to STAND_BY for all instances.
ALTER TABLE platform_instance ADD COLUMN state VARCHAR(16) NOT NULL DEFAULT 'STAND_BY';

-- Update state to LEADER for the instance which is currently the leader.
UPDATE platform_instance SET state = 'LEADER' WHERE is_leader = true;
-- Drop the is_leader column as it is no longer needed.
ALTER TABLE platform_instance DROP COLUMN is_leader;

-- Drop is_local and rename is_local_new to is_local to drop the unique constraint.
ALTER TABLE platform_instance ADD COLUMN is_local_new BOOLEAN NOT NULL DEFAULT false;
UPDATE platform_instance SET is_local_new = true WHERE is_local = true;
ALTER TABLE platform_instance DROP COLUMN is_local;
ALTER TABLE platform_instance RENAME COLUMN is_local_new TO is_local;