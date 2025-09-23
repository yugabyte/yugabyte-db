-- Copyright (c) YugaByte, Inc.

ALTER TABLE users ADD COLUMN api_token_version bigint DEFAULT 0 NOT NULL;
