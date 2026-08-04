-- Copyright (c) YugaByte, Inc.


ALTER TABLE IF EXISTS restore
      ADD COLUMN IF NOT EXISTS alter_load_balancer BOOLEAN DEFAULT TRUE NOT NULL;
