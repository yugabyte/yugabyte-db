-- Copyright (c) YugabyteDB, Inc.

-- Adding the completion_time column to backup table.
ALTER TABLE backup ADD COLUMN completion_time timestamp;