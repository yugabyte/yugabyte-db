-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS image_bundle ADD COLUMN IF NOT EXISTS metadata json_alias;
