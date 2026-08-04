-- Copyright (c) YugaByte, Inc.

-- Rename the column to skip update of values.
ALTER TABLE task_info RENAME COLUMN details to task_params;
-- Add a new column.
ALTER TABLE task_info ADD COLUMN details JSON_ALIAS;
