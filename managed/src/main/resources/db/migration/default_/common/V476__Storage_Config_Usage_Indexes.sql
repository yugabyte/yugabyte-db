-- Copyright (c) YugabyteDB, Inc.

-- The v2 storage config list reports which backups, schedules and DR configs use each config, and
-- filters on it. Without these, each lookup scans the backup and dr_config tables.
CREATE INDEX IF NOT EXISTS ix_backup_storage_config_uuid ON backup (storage_config_uuid);
CREATE INDEX IF NOT EXISTS ix_dr_config_storage_config_uuid ON dr_config (storage_config_uuid);
