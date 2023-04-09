-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS kms_history ADD COLUMN IF NOT EXISTS re_encryption_count INT DEFAULT 0 NOT NULL;

-- Update the primary key to include re_encryption_count for master key rotation.
-- We need to add re_encryption_count to the primary key because if 2 KMS configs point to the
-- same master key, the key_ref will be the same. This is specifically for master key rotation.
ALTER TABLE kms_history DROP CONSTRAINT pk_kms_universe_key_history;
ALTER TABLE kms_history ADD CONSTRAINT pk_kms_universe_key_history PRIMARY KEY (target_uuid, type, key_ref, re_encryption_count);

-- Since we need to send <key_ref, universe_key> to the DB, we need to keep track of original key_ref to use as db_key_id.
-- We propagate that to the new records that are added when doing re-encryption from master key rotation / restores.
ALTER TABLE IF EXISTS kms_history ADD COLUMN IF NOT EXISTS db_key_id varchar;
UPDATE kms_history SET db_key_id=key_ref WHERE db_key_id IS NULL;
