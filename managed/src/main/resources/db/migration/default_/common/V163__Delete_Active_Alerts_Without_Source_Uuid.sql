-- Copyright (c) YugaByte, Inc.

-- Clean old alerts with source_uuid = null which are not yet resolved -
-- as attempt to resolve them fails.
-- In case alert condition still fires - it will be raised again.
delete from alert where source_uuid is null and state != 'RESOLVED';

