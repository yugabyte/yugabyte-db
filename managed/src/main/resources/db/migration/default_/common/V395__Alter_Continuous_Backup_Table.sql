-- Copyright (c) YugaByte, Inc.

ALTER TABLE continuous_backup_config
ADD COLUMN storage_location VARCHAR(1024);

ALTER TABLE continuous_backup_config
ADD COLUMN last_backup BIGINT;