-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS customer_task ADD COLUMN IF NOT EXISTS user_email varchar(255);
