-- Copyright (c) YugaByte, Inc.

-- Add version column to provider table
ALTER TABLE IF EXISTS provider
    ADD COLUMN IF NOT EXISTS version BIGINT DEFAULT 0 NOT NULL;
