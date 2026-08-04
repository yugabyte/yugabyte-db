-- Copyright (c) YugaByte, Inc.

-- Add fields host_vpc_id, dest_vpc_id, host_vpc_region
ALTER TABLE IF EXISTS provider
    ADD COLUMN IF NOT EXISTS host_vpc_id VARCHAR(100) DEFAULT NULL;
ALTER TABLE IF EXISTS provider
    ADD COLUMN IF NOT EXISTS dest_vpc_id VARCHAR(100) DEFAULT NULL;
ALTER TABLE IF EXISTS provider
    ADD COLUMN IF NOT EXISTS host_vpc_region VARCHAR(256) DEFAULT NULL;
