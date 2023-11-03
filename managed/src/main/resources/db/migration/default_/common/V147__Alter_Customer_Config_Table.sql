-- Copyright (c) YugaByte, Inc.

ALTER TABLE customer_config ADD COLUMN IF NOT EXISTS state varchar(50) DEFAULT 'Active' NOT NULL;
