-- Copyright (c) YugaByte, Inc.

-- Add column base_backup_uuid to backup table.
ALTER TABLE backup ADD COLUMN if not exists base_backup_uuid uuid;
UPDATE backup set base_backup_uuid = backup_uuid where base_backup_uuid is null;
ALTER TABLE backup ALTER COLUMN base_backup_uuid set not null;
