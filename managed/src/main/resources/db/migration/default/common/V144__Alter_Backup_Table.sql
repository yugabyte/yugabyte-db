ALTER TABLE backup ADD COLUMN IF NOT EXISTS category varchar(50) DEFAULT 'YB_BACKUP_SCRIPT';
ALTER TABLE backup ADD COLUMN IF NOT EXISTS version varchar(50) DEFAULT 'V1';

ALTER TABLE backup ADD CONSTRAINT ck_backup_category check (category in ('YB_BACKUP_SCRIPT', 'YB_CONTROLLER'));
ALTER TABLE backup ADD CONSTRAINT ck_backup_version check (version in ('V1', 'V2'));
