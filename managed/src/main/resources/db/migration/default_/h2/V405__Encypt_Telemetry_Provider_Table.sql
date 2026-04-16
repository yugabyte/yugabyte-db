-- Copyright (c) YugaByte, Inc.

-- Alter column type to bytea without encryption specifically for unit tests in h2 DB.
ALTER TABLE telemetry_provider ALTER COLUMN config TYPE binary varying;
