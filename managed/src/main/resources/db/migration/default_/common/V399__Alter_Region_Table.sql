-- Copyright (c) YugaByte, Inc.

ALTER TABLE region DROP CONSTRAINT IF EXISTS provider_uuid_code_unique;
ALTER TABLE region ADD CONSTRAINT provider_uuid_code_unique UNIQUE (provider_uuid, code);