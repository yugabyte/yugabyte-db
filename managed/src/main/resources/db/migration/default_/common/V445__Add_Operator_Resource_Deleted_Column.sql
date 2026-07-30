-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS operator_resource ADD COLUMN IF NOT EXISTS deleted BOOLEAN NOT NULL DEFAULT FALSE;
ALTER TABLE IF EXISTS operator_resource ADD COLUMN IF NOT EXISTS platform_instances TEXT;
