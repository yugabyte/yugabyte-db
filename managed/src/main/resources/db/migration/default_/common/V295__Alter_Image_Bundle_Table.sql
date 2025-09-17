-- Copyright (c) YugabyteDB, Inc.

ALTER TABLE IF EXISTS image_bundle ADD COLUMN IF NOT EXISTS active boolean;

UPDATE image_bundle SET active = true;