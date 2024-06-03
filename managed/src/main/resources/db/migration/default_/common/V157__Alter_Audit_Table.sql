ALTER TABLE audit RENAME COLUMN target_uuid to target_id;
ALTER TABLE audit ALTER COLUMN target_id TYPE TEXT;