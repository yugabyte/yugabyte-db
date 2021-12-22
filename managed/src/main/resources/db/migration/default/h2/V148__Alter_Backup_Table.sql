-- Copyright (c) YugaByte, Inc.

-- Adding the universe_uuid column to backup table.
ALTER TABLE backup ADD COLUMN universe_uuid uuid;
ALTER TABLE backup ALTER COLUMN universe_uuid SET not null;

-- Adding the config_uuid column to backup table.
ALTER TABLE backup ADD COLUMN storage_config_uuid uuid;
ALTER TABLE backup ALTER COLUMN storage_config_uuid SET not null;

-- Adding the universe_name column to backup table.
ALTER TABLE backup ADD COLUMN universe_name varchar(255);
