-- Copyright (c) YugaByte, Inc.
ALTER TABLE high_availability_config
ADD COLUMN IF NOT EXISTS accept_any_certificate BOOLEAN DEFAULT false;