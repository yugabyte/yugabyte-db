-- Copyright (c) YugaByte, Inc.

-- Add column if it does not exist
ALTER TABLE IF EXISTS kms_history 
ADD COLUMN IF NOT EXISTS encryption_context json_alias;

-- Update existing rows to ensure they have a default empty object before conversion
UPDATE kms_history 
SET encryption_context = '{}' 
WHERE encryption_context IS NULL;

-- Alter column type to bytea without encryption specifically for unit tests in h2 DB.
ALTER TABLE kms_history ALTER COLUMN encryption_context TYPE binary varying;
