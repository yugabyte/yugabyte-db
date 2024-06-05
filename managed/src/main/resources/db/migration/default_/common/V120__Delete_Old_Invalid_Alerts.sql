-- Copyright (c) YugaByte, Inc.

-- Clean old alerts to avoid inconsistent alert structure.
-- In case alert condition is still valid - it will be raised again.
-- This will also help to clean up old alerts, which had no lifecycle and are always ACTIVE
delete from alert
 where name is null
  or message is null
  or definition_uuid is null
  or configuration_uuid is null
  or configuration_type is null
  or source_name is null;
