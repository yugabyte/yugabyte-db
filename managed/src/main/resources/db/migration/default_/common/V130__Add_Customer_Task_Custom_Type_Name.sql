-- Copyright (c) YugaByte, Inc.

ALTER TABLE customer_task ADD COLUMN IF NOT EXISTS custom_type_name varchar(50);
