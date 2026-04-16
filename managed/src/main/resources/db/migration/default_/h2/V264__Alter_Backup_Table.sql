-- Copyright (c) YugaByte, Inc.

-- Adding the has_kms_history column to backup table.
ALTER TABLE backup ADD COLUMN if not exists has_kms_history boolean default false;
