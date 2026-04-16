-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS dr_config ADD COLUMN IF NOT EXISTS storage_config_uuid UUID;
ALTER TABLE IF EXISTS dr_config
    DROP CONSTRAINT IF EXISTS fk_dr_config_customer_config;
ALTER TABLE IF EXISTS dr_config 
    ADD CONSTRAINT fk_dr_config_customer_config 
    FOREIGN KEY(storage_config_uuid) REFERENCES customer_config(config_uuid);

ALTER TABLE IF EXISTS dr_config ADD COLUMN IF NOT EXISTS parallelism INT NOT NULL DEFAULT 8;

