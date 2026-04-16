-- Copyright (c) YugaByte, Inc.

-- Add column if it does not exist
ALTER TABLE IF EXISTS kms_history 
ADD COLUMN IF NOT EXISTS encryption_context json_alias;

-- Update existing rows to ensure they have a default empty object before conversion
UPDATE kms_history 
SET encryption_context = '{}' 
WHERE encryption_context IS NULL;

-- Alter column type to bytea with encryption
ALTER TABLE kms_history 
ALTER COLUMN encryption_context 
SET DATA TYPE bytea 
USING pgp_sym_encrypt(encryption_context::text, 'kms_history::encryption_context');

-- Set a default empty encrypted JSON object for new rows
ALTER TABLE kms_history 
ALTER COLUMN encryption_context 
SET DEFAULT pgp_sym_encrypt('{}', 'kms_history::encryption_context');
