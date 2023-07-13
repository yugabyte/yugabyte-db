-- Copyright (c) YugaByte, Inc.

ALTER TABLE alert_definition DROP COLUMN query;
UPDATE alert_definition SET config_written = false;
