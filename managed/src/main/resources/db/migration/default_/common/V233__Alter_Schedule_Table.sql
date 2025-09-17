-- Copyright (c) YugabyteDB, Inc.

ALTER TABLE IF EXISTS schedule ADD COLUMN IF NOT EXISTS user_email varchar(255);
