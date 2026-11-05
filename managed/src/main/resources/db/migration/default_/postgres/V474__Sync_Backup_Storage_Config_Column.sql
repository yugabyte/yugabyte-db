-- Copyright (c) YugabyteDB, Inc.

-- Deleting a backup with a storage config other than its own changed the config in backup_info
-- but not the storage_config_uuid column. Where the backup was not deleted after all, the two
-- disagree. Restore and delete use backup_info, so align the column with it.
UPDATE backup
SET storage_config_uuid = uuid(backup_info ->> 'storageConfigUUID')
WHERE backup_info ->> 'storageConfigUUID' IS NOT NULL
  AND storage_config_uuid <> uuid(backup_info ->> 'storageConfigUUID');
