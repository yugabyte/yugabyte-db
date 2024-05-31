-- Copyright (c) YugaByte, Inc.

ALTER TABLE alert_configuration ADD COLUMN IF NOT EXISTS labels TEXT;
