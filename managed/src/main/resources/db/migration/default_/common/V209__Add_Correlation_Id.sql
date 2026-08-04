-- Copyright (c) YugaByte, Inc.

-- Add correlation id column to customer task table
ALTER TABLE IF EXISTS customer_task
    ADD COLUMN IF NOT EXISTS correlation_id VARCHAR(64) DEFAULT NULL;
